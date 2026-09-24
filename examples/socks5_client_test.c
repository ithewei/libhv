/*
 * TCP client via SOCKS5 proxy (pure C, io layer).
 *
 * Demonstrates routing a connection through a SOCKS5 proxy with hio_set_proxy.
 * The socket is created for the PROXY address; the proxy issues a CONNECT to
 * the target carried in proxy_setting_t (target host sent as a domain name so
 * the proxy resolves it, or as ATYP=ipv4/ipv6 for a numeric literal).
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
    // Route through the SOCKS5 proxy. The factory connects to proxy_host:proxy_port
    // and completes SOCKS5 CONNECT to target_host:target_port before on_connect.
    proxy_setting_t proxy;
    memset(&proxy, 0, sizeof(proxy));
    hv_strncpy(proxy.proxy_host, proxy_host, sizeof(proxy.proxy_host));
    proxy.proxy_port = proxy_port;
    hv_strncpy(proxy.target_host, target_host, sizeof(proxy.target_host));
    proxy.target_port = target_port;
    if (user) hv_strncpy(proxy.username, user, sizeof(proxy.username));
    if (pass) hv_strncpy(proxy.password, pass, sizeof(proxy.password));
    hio_t* io = hloop_create_socks5_client(loop, &proxy, on_connect, on_close);
    if (io == NULL) {
        printf("create socks5 client failed\n");
        hloop_free(&loop);
        return -1;
    }

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
