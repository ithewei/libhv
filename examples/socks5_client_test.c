/*
 * TCP client via SOCKS5 proxy (pure C, io layer).
 *
 * Demonstrates routing a connection through a SOCKS5 proxy with hio_set_socks5.
 * The target host is sent to the proxy as a domain name (the proxy resolves it).
 *
 * @build   make examples
 * @test    # start libhv's own SOCKS5 proxy server as the proxy:
 *          bin/socks5_proxy_server 1080
 *          # then connect to an echo server through it:
 *          bin/tcp_echo_server 1234
 *          bin/socks5_client_test 127.0.0.1 1080 127.0.0.1 1234
 *
 * @example bin/socks5_client_test <proxy_host> <proxy_port> <target_host> <target_port> [user] [pass]
 */

#include "hloop.h"
#include "hbase.h"

static void on_close(hio_t* io) {
    printf("disconnected: connfd=%d error=%d\n", hio_fd(io), hio_error(io));
    hloop_stop(hevent_loop(io));
}

static void on_message(hio_t* io, void* buf, int len) {
    printf("recv: %.*s", len, (char*)buf);
}

static void on_connect(hio_t* io) {
    printf("connected through socks5 proxy, send hello\n");
    hio_setcb_read(io, on_message);
    hio_read(io);
    hio_write(io, "hello via socks5\n", 17);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: %s proxy_host proxy_port target_host target_port [user] [pass]\n", argv[0]);
        return -1;
    }
    const char* proxy_host = argv[1];
    int proxy_port = atoi(argv[2]);
    const char* target_host = argv[3];
    int target_port = atoi(argv[4]);
    const char* user = argc > 5 ? argv[5] : NULL;
    const char* pass = argc > 6 ? argv[6] : NULL;

    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    // NOTE: create the socket for the real target; the proxy handshake connects
    // to the proxy and issues CONNECT to this target.
    hio_t* io = hio_create_socket(loop, target_host, target_port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) {
        printf("create socket failed\n");
        return -1;
    }

    // route through the SOCKS5 proxy
    socks5_setting_t socks5;
    memset(&socks5, 0, sizeof(socks5));
    hv_strncpy(socks5.host, proxy_host, sizeof(socks5.host));
    socks5.port = proxy_port;
    if (user) hv_strncpy(socks5.username, user, sizeof(socks5.username));
    if (pass) hv_strncpy(socks5.password, pass, sizeof(socks5.password));
    hio_set_socks5(io, &socks5);

    hio_setcb_connect(io, on_connect);
    hio_setcb_close(io, on_close);
    hio_connect(io);

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
