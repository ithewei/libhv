#include "hloop.h"
#include "hsocket.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", io->fd, io->error);
}

void on_recvfrom(hio_t* io, void* buf, int readbytes) {
    printf("on_recvfrom fd=%d readbytes=%d\n", io->fd, readbytes);
    char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
    char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
    printf("[%s] <=> [%s]\n",
            sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
            sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
    printf("< %s\n", buf);
    // echo
    printf("> %s\n", buf);
    hsendto(io->loop, io->fd, buf, readbytes, NULL);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    hloop_t loop;
    hloop_init(&loop);
    hio_t* io = create_udp_server(&loop, port);
    if (io == NULL) {
        return -20;
    }
    io->close_cb = on_close;
    hrecvfrom(&loop, io->fd, recvbuf, RECV_BUFSIZE, on_recvfrom);
    hloop_run(&loop);
    return 0;
}
