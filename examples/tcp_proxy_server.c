/*
 * tcp proxy server
 *
 * @build:        make clean && make examples WITH_OPENSSL=yes
 * @http_server:  bin/httpd -s restart -d
 * @proxy_server: bin/tcp_proxy_server 8888 127.0.0.1:8080
 *                bin/tcp_proxy_server 8888 127.0.0.1:8443
 *                bin/tcp_proxy_server 8888 www.baidu.com
 *                bin/tcp_proxy_server 8888 www.baidu.com:443
 * @client:       bin/curl -v 127.0.0.1:8888
 *                bin/nc 127.0.0.1 8888
 *                > GET / HTTP/1.1
 *                > Connection: close
 *                > [Enter]
 *                > GET / HTTP/1.1
 *                > Connection: keep-alive
 *                > [Enter]
 */

#include "hloop.h"
#include "hsocket.h"

static char proxy_host[64] = "127.0.0.1";
static int  proxy_port = 80;
static int  proxy_ssl = 0;

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

    if (proxy_port % 1000 == 443) proxy_ssl = 1;
    hio_setup_tcp_upstream(io, proxy_host, proxy_port, proxy_ssl);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s port proxy_host:proxy_port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);
    char* pos = strchr(argv[2], ':');
    if (pos) {
        int len = pos - argv[2];
        if (len > 0) {
            memcpy(proxy_host, argv[2], len);
            proxy_host[len] = '\0';
        }
        proxy_port = atoi(pos + 1);
    } else {
        strncpy(proxy_host, argv[2], sizeof(proxy_host));
    }
    if (proxy_port == 0) proxy_port = 80;
    printf("proxy: [%s:%d]\n", proxy_host, proxy_port);

    hloop_t* loop = hloop_new(0);
    hio_t* listenio = hloop_create_tcp_server(loop, "0.0.0.0", port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", hio_fd(listenio));
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
