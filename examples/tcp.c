#include "hloop.h"
#include "hsocket.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", io->fd, io->error);
}

void on_recv(hio_t* io, void* buf, int readbytes) {
    printf("on_recv fd=%d readbytes=%d\n", io->fd, readbytes);
    char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
    char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
    printf("[%s] <=> [%s]\n",
            sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
            sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
    printf("< %s\n", buf);
    // echo
    printf("> %s\n", buf);
    hsend(io->loop, io->fd, buf, readbytes, NULL);
}

void on_accept(hio_t* io, int connfd) {
    printf("on_accept listenfd=%d connfd=%d\n", io->fd, connfd);
    char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
    char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
    printf("accept listenfd=%d connfd=%d [%s] <= [%s]\n", io->fd, connfd,
            sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
            sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));

    hio_t* connio = hrecv(io->loop, connfd, recvbuf, RECV_BUFSIZE, on_recv);
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
    hio_t* listenio = create_tcp_server(&loop, port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", listenio->fd);
    hloop_run(&loop);
    return 0;
}
