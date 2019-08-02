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

void on_stdin(hio_t* io, void* buf, int readbytes) {
    printf("on_stdin fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("> %s\n", buf);

    hio_t* iosock = (hio_t*)io->userdata;
    hwrite(iosock->loop, iosock->fd, buf, readbytes, on_write);
}

void on_read(hio_t* io, void* buf, int readbytes) {
    printf("on_read fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("< %s\n", buf);
    printf(">>");
    fflush(stdout);
}

void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", io->fd, io->error);
    hio_t* iostdin = (hio_t*)io->userdata;
    hio_del(iostdin, READ_EVENT);
}

void on_connect(hio_t* io, int state) {
    printf("on_connect fd=%d state=%d\n", io->fd, state);
    if (state == 0) {
        printf("error=%d:%s\n", io->error, strerror(io->error));
        return;
    }
    struct sockaddr_in* localaddr = (struct sockaddr_in*)io->localaddr;
    struct sockaddr_in* peeraddr = (struct sockaddr_in*)io->peeraddr;
    char localip[64];
    char peerip[64];
    inet_ntop(localaddr->sin_family, &localaddr->sin_addr, localip, sizeof(localip));
    inet_ntop(peeraddr->sin_family, &peeraddr->sin_addr, peerip, sizeof(peerip));
    printf("connect connfd=%d [%s:%d] => [%s:%d]\n", io->fd,
            localip, ntohs(localaddr->sin_port),
            peerip, ntohs(peeraddr->sin_port));

    // NOTE: just on loop, readbuf can be shared.
    hio_t* iostdin = hread(io->loop, 0, readbuf, RECV_BUFSIZE, on_stdin);
    iostdin->userdata = io;
    hio_t* iosock = hread(io->loop, io->fd, readbuf, RECV_BUFSIZE, on_read);
    iosock->close_cb = on_close;
    iosock->userdata = iostdin;

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

    hloop_t loop;
    hloop_init(&loop);
    //hidle_add(&loop, on_idle, INFINITE);
    //htimer_add(&loop, on_timer, 1000, INFINITE);
    hio_t* io = hconnect(&loop, host, port, on_connect);
    if (io == NULL) {
        return -20;
    }
    printf("connfd=%d\n", io->fd);
    hloop_run(&loop);

    return 0;
}
