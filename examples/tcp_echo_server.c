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
#define TEST_SSL        0
#define TEST_READ_ONCE  0
#define TEST_READ_UNTIL 0
#define TEST_READ_STOP  0
#define TEST_UNPACK     0

#if TEST_UNPACK
unpack_setting_t unpack_setting;
#endif

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

#if TEST_READ_STOP
    hio_read_stop(io);
#elif TEST_READ_ONCE
    hio_read_once(io);
#elif TEST_READ_UNTIL
    hio_read_until(io, TEST_READ_UNTIL);
#endif
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

#if TEST_UNPACK
    hio_set_unpack(io, &unpack_setting);
#endif

#if TEST_READ_ONCE
    hio_read_once(io);
#elif TEST_READ_UNTIL
    hio_read_until(io, TEST_READ_UNTIL);
#else
    hio_read_start(io);
#endif
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

#if TEST_SSL
    hssl_ctx_init_param_t ssl_param;
    memset(&ssl_param, 0, sizeof(ssl_param));
    ssl_param.crt_file = "cert/server.crt";
    ssl_param.key_file = "cert/server.key";
    ssl_param.endpoint = HSSL_SERVER;
    if (hssl_ctx_init(&ssl_param) == NULL) {
        fprintf(stderr, "hssl_ctx_init failed!\n");
        return -30;
    }
#endif

#if TEST_UNPACK
    memset(&unpack_setting, 0, sizeof(unpack_setting_t));
    unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
    unpack_setting.mode = UNPACK_BY_DELIMITER;
    unpack_setting.delimiter[0] = '\r';
    unpack_setting.delimiter[1] = '\n';
    unpack_setting.delimiter_bytes = 2;
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
