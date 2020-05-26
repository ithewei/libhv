#include "hloop.h"
#include "hsocket.h"

const char* path = NULL;

void cleanup(int signo) {
    if (path) {
        printf("cleaning up: %s\n", path);
        unlink(path);
    }
    exit(0);
}

void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

void on_recvfrom(hio_t* io, void* buf, int readbytes) {
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
    int port;
    if (argc == 2) {
        port = atoi(argv[1]);
    }
    else if (argc == 3 && strcmp(argv[1], "--unix") == 0) {
        path = argv[2];
    }
    else {
        printf("Usage: cmd port\n"
               "       cmd --unix path\n");
        return -10;
    };

    hloop_t* loop = hloop_new(0);
    hio_t* io = NULL;
    if (path) {
#ifdef HAVE_UDS
        io = create_unix_dgram_server(loop, path);
#else
        printf("Unix domain socket is not supported!\n");
#endif
    } else {
        io = create_udp_server(loop, "0.0.0.0", port);
    }
    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recvfrom);
    hio_read(io);

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
