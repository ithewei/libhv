/*
 * tcp proxy server
 *
 * @build:        make clean && make examples WITH_OPENSSL=yes
 * @http_server:  bin/httpd -s restart -d
 * @proxy_server: bin/tcp_proxy_server 1080 127.0.0.1:8080
 *                bin/tcp_proxy_server 1080 127.0.0.1:8443
 *                bin/tcp_proxy_server 1080 www.baidu.com
 *                bin/tcp_proxy_server 1080 www.baidu.com:443
 * @client:       bin/curl -v 127.0.0.1:1080
 *                bin/nc 127.0.0.1 1080
 *                > GET / HTTP/1.1
 *                > Connection: close
 *                > [Enter]
 *                > GET / HTTP/1.1
 *                > Connection: keep-alive
 *                > [Enter]
 *
 * @benchmark:    sudo apt install iperf
 *                iperf -s -p 5001
 *                bin/tcp_proxy_server 1212 127.0.0.1:5001
 *                iperf -c 127.0.0.1 -p 5001 -l 8K
 *                iperf -c 127.0.0.1 -p 1212 -l 8K
 */

#include "hloop.h"
#include "hsocket.h"

static char proxy_host[64] = "0.0.0.0";
static int  proxy_port = 1080;
static int  proxy_ssl = 0;

static char backend_host[64] = "127.0.0.1";
static int  backend_port = 80;
static int  backend_ssl = 0;

// hloop_create_tcp_server -> on_accept -> hio_setup_tcp_upstream

static void on_accept(hio_t* io) {
    /*
    printf("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    */

    if (backend_port % 1000 == 443) backend_ssl = 1;
    hio_setup_tcp_upstream(io, backend_host, backend_port, backend_ssl);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s proxy_port backend_host:backend_port\n", argv[0]);
        return -10;
    }
    proxy_port = atoi(argv[1]);
    if (proxy_port % 1000 == 443) proxy_ssl = 1;
    char* pos = strchr(argv[2], ':');
    if (pos) {
        int len = pos - argv[2];
        if (len > 0) {
            memcpy(backend_host, argv[2], len);
            backend_host[len] = '\0';
        }
        backend_port = atoi(pos + 1);
    } else {
        strncpy(backend_host, argv[2], sizeof(backend_host));
    }
    if (backend_port == 0) backend_port = 80;
    printf("%s:%d proxy %s:%d\n", proxy_host, proxy_port, backend_host, backend_port);

    hloop_t* loop = hloop_new(0);
    hio_t* listenio = hloop_create_tcp_server(loop, proxy_host, proxy_port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", hio_fd(listenio));
    if (proxy_ssl) {
        hio_enable_ssl(listenio);
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
