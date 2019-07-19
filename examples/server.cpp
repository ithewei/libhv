#include "hloop.h"
#include "hsocket.h"

#define RECV_BUFSIZE    4096
static char readbuf[RECV_BUFSIZE];

void on_timer(htimer_t* timer) {
    static int cnt = 0;
    printf("on_timer timer_id=%lu time=%lus cnt=%d\n", timer->event_id, hloop_now(timer->loop), ++cnt);
}

void on_idle(hidle_t* idle) {
    static int cnt = 0;
    printf("on_idle idle_id=%lu cnt=%d\n", idle->event_id, ++cnt);
}

void on_write(hio_t* io, const void* buf, int writebytes) {
    printf("on_write fd=%d writebytes=%d\n", io->fd, writebytes);
}

void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", io->fd, io->error);
}

void on_write(hio_t* io, const void* buf, int writebytes, void* userdata) {
    printf("on_write fd=%d writebytes=%d\n", io->fd, writebytes);
}

void on_read(hio_t* io, void* buf, int readbytes) {
    printf("on_read fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("< %s\n", buf);
    // echo
    printf("> %s\n", buf);
    hwrite(io->loop, io->fd, buf, readbytes, on_write);
}

void on_accept(hio_t* io, int connfd) {
    printf("on_accept listenfd=%d connfd=%d\n", io->fd, connfd);
    struct sockaddr_in localaddr, peeraddr;
    socklen_t addrlen;
    addrlen = sizeof(struct sockaddr_in);
    getsockname(connfd, (struct sockaddr*)&localaddr, &addrlen);
    addrlen = sizeof(struct sockaddr_in);
    getpeername(connfd, (struct sockaddr*)&peeraddr, &addrlen);
    printf("accept connfd=%d [%s:%d] <= [%s:%d]\n", connfd,
            inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port),
            inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

    nonblocking(connfd);
    // one loop can use one readbuf
    hio_t* connio = hread(io->loop, connfd, readbuf, RECV_BUFSIZE, on_read);
    connio->close_cb = on_close;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    int listenfd = Listen(port);
    printf("listenfd=%d\n", listenfd);
    if (listenfd < 0) {
        return listenfd;
    }

    hloop_t loop;
    hloop_init(&loop);
    //hidle_add(&loop, on_idle, INFINITE);
    //htimer_add(&loop, on_timer, 1000, INFINITE);
    haccept(&loop, listenfd, on_accept);
    hloop_run(&loop);
}
