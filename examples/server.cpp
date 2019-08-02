#include "hloop.h"

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

void on_read(hio_t* io, void* buf, int readbytes) {
    printf("on_read fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("< %s\n", buf);
    // echo
    printf("> %s\n", buf);
    hwrite(io->loop, io->fd, buf, readbytes, on_write);
}

void on_accept(hio_t* io, int connfd) {
    printf("on_accept listenfd=%d connfd=%d\n", io->fd, connfd);
    struct sockaddr_in* localaddr = (struct sockaddr_in*)io->localaddr;
    struct sockaddr_in* peeraddr = (struct sockaddr_in*)io->peeraddr;
    char localip[64];
    char peerip[64];
    inet_ntop(localaddr->sin_family, &localaddr->sin_addr, localip, sizeof(localip));
    inet_ntop(peeraddr->sin_family, &peeraddr->sin_addr, peerip, sizeof(peerip));
    printf("accept listenfd=%d connfd=%d [%s:%d] <= [%s:%d]\n", io->fd, connfd,
            localip, ntohs(localaddr->sin_port),
            peerip, ntohs(peeraddr->sin_port));

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

    hloop_t loop;
    hloop_init(&loop);
    //hidle_add(&loop, on_idle, INFINITE);
    //htimer_add(&loop, on_timer, 1000, INFINITE);
    hio_t* io = hlisten(&loop, port, on_accept);
    if (io == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", io->fd);
    hloop_run(&loop);
}
