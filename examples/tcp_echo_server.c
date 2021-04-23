/*
 * tcp echo server
 *
 * @build   make examples
 * @server  bin/tcp_echo_server 1234
 * @client  bin/nc 127.0.0.1 1234
 *          nc     127.0.0.1 1234
 *          telnet 127.0.0.1 1234
 */

#include "hloop.h"
#include "hsocket.h"
#include "hssl.h"

/*
 * @test    ssl_server
 * #define  TEST_SSL 1
 *
 * @build   ./configure --with-openssl && make clean && make
 *
 */
#define TEST_SSL 0

// hloop_create_tcp_server -> on_accept -> hio_read -> on_recv -> hio_write

static void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
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

static void on_accept(hio_t* io) {
    printf("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

#if TEST_SSL
    {
        hssl_ctx_init_param_t param;
        memset(&param, 0, sizeof(param));
        param.crt_file = "cert/server.crt";
        param.key_file = "cert/server.key";
        if (hssl_ctx_init(&param) == NULL) {
            fprintf(stderr, "SSL certificate verify failed!\n");
            return -30;
        }
    }
#endif

    hloop_t* loop = hloop_new(0);
#if TEST_SSL
    hio_t* listenio = hloop_create_ssl_server(loop, "0.0.0.0", port, on_accept);
#else
    hio_t* listenio = hloop_create_tcp_server(loop, "0.0.0.0", port, on_accept);
#endif
    if (listenio == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", hio_fd(listenio));
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
