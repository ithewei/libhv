#include "hloop.h"
#include "hio.h"
#include "hsocket.h"

#define RECV_BUFSIZE    4096
static char readbuf[RECV_BUFSIZE];

void on_timer(htimer_t* timer, void* userdata) {
    static int cnt = 0;
    printf("on_timer timer_id=%d time=%luus cnt=%d\n", timer->timer_id, timer->loop->cur_time, ++cnt);
}

void on_idle(hidle_t* idle, void* userdata) {
    static int cnt = 0;
    printf("on_idle idle_id=%d cnt=%d\n", idle->idle_id, ++cnt);
}

void on_write(hio_t* io, const void* buf, int writebytes, void* userdata) {
    printf("on_write fd=%d writebytes=%d\n", io->fd, writebytes);
}

void on_stdin(hio_t* io, void* buf, int readbytes, void* userdata) {
    printf("on_stdin fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("> %s\n", io->readbuf);

    hio_t* iosock = (hio_t*)io->read_userdata;
    hwrite(iosock->loop, iosock->fd, io->readbuf, readbytes, on_write, NULL);
}

void on_read(hio_t* io, void* buf, int readbytes, void* userdata) {
    printf("on_read fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("< %s\n", io->readbuf);
    printf(">>");
    fflush(stdout);
}

void on_close(hio_t* io, void* userdata) {
    printf("on_close fd=%d\n", io->fd);
    hio_t* iostdin = (hio_t*)userdata;
    hio_del(iostdin);
}

void on_connect(hio_t* io, int state, void* userdata) {
    printf("on_connect fd=%d state=%d\n", io->fd, state);
    if (state == 0) return;
    struct sockaddr_in localaddr, peeraddr;
    socklen_t addrlen;
    addrlen = sizeof(struct sockaddr_in);
    getsockname(io->fd, (struct sockaddr*)&localaddr, &addrlen);
    addrlen = sizeof(struct sockaddr_in);
    getpeername(io->fd, (struct sockaddr*)&peeraddr, &addrlen);
    printf("connect connfd=%d [%s:%d] => [%s:%d]\n", io->fd,
            inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port),
            inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

    // NOTE: just on loop, readbuf can be shared.
    hio_t* iostdin = hread(io->loop, 0, readbuf, RECV_BUFSIZE, on_stdin, io);
    hread(io->loop, io->fd, readbuf, RECV_BUFSIZE, on_read, NULL, on_close, iostdin);

    printf(">>");
    fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: cmd host port\n");
        return -10;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);

    int connfd = Connect(host, port, 1);
    printf("connfd=%d\n", connfd);
    if (connfd < 0) {
        return connfd;
    }

    hloop_t loop;
    hloop_init(&loop);
    //hidle_add(&loop, on_idle, NULL);
    //htimer_add(&loop, on_timer, NULL, 1000, INFINITE);
    hconnect(&loop, connfd, on_connect, NULL);
    hloop_run(&loop);

    return 0;
}
