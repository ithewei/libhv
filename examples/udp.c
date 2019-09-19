#include "hloop.h"
#include "hsocket.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

void on_recvfrom(hio_t* io, void* buf, int readbytes) {
    printf("on_recvfrom fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
    char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
    printf("[%s] <=> [%s]\n",
            sockaddr_snprintf(hio_localaddr(io), localaddrstr, sizeof(localaddrstr)),
            sockaddr_snprintf(hio_peeraddr(io), peeraddrstr, sizeof(peeraddrstr)));
    printf("< %s\n", buf);
    // echo
    printf("> %s\n", buf);
    hio_write(io, buf, readbytes);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    hloop_t* loop = hloop_new(0);
    hio_t* io = create_udp_server(loop, "0.0.0.0", port);
    if (io == NULL) {
        return -20;
    }
    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recvfrom);
    hio_set_readbuf(io, recvbuf, RECV_BUFSIZE);
    hio_read(io);
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
