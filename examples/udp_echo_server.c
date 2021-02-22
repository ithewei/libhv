/*
 * udp echo server
 *
 * @build   make examples
 * @server  bin/udp_echo_server 1234
 * @client  bin/nc -u 127.0.0.1 1234
 *          nc     -u 127.0.0.1 1234
 *
 */

#include "hloop.h"
#include "hsocket.h"

static void on_recvfrom(hio_t* io, void* buf, int readbytes) {
    printf("on_recvfrom fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("[%s] <=> [%s]\n",
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    printf("< %.*s", readbytes, (char*)buf);
    // echo
    printf("> %.*s", readbytes, (char*)buf);
    hio_write(io, buf, readbytes);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    hloop_t* loop = hloop_new(0);
    hio_t* io = hloop_create_udp_server(loop, "0.0.0.0", port);
    if (io == NULL) {
        return -20;
    }
    hio_setcb_read(io, on_recvfrom);
    hio_read(io);
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
