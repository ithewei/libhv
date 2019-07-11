#include "hloop.h"
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

void on_close(hio_t* io, void* userdata) {
    printf("on_close fd=%d\n", io->fd);
}

void on_write(hio_t* io, const void* buf, int writebytes, void* userdata) {
    printf("on_write fd=%d writebytes=%d\n", io->fd, writebytes);
}

void on_read(hio_t* io, void* buf, int readbytes, void* userdata) {
    printf("on_read fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("< %s\n", io->readbuf);
    // echo
    printf("> %s\n", io->readbuf);
    hwrite(io->loop, io->fd, io->readbuf, readbytes, on_write, NULL);
}

void on_accept(hio_t* io, int connfd, void* userdata) {
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
    hread(io->loop, connfd, readbuf, RECV_BUFSIZE, on_read, NULL, on_close, NULL);
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
    //hidle_add(&loop, on_idle, NULL);
    //htimer_add(&loop, on_timer, NULL, 1000, INFINITE);
    haccept(&loop, listenfd, on_accept, NULL);
    hloop_run(&loop);
}
