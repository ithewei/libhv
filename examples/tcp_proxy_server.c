/*
 * tcp proxy server
 *
 * @build:        make examples
 * @http_server:  bin/httpd -d
 * @proxy_server: bin/tcp_proxy_server 1234 127.0.0.1:8080
 *                bin/tcp_proxy_server 1234 www.baidu.com
 * @client:       bin/curl -v 127.0.0.1:1234
 *                bin/nc 127.0.0.1 1234
 *                > GET / HTTP/1.1
 *                > Connection: close
 *                > [Enter]
 *                > GET / HTTP/1.1
 *                > Connection: keep-alive
 *                > [Enter]
 */

#include "hloop.h"
#include "hsocket.h"

// hloop_create_tcp_server
// on_accept(connio) => proxyio = hloop_create_tcp_client
// on_proxy_connect(proxyio) => hio_read(connio) hio_read(proxyio)
// on_recv(connio) => hio_write(proxyio)
// on_proxy_recv(proxyio) => hio_write(connio)
// on_close(connio) => hio_close(proxyio)
// on_proxy_close(proxyio) => hio_close(connio)

static char proxy_host[64] = "127.0.0.1";
static int proxy_port = 80;

static void on_proxy_close(hio_t* proxyio) {
    hio_t* connio = (hio_t*)hevent_userdata(proxyio);
    if (connio) {
        hevent_set_userdata(proxyio, NULL);
        hio_close(connio);
    }
}

static void on_proxy_recv(hio_t* proxyio, void* buf, int readbytes) {
    hio_t* connio = (hio_t*)hevent_userdata(proxyio);
    assert(connio != NULL);
    hio_write(connio, buf, readbytes);
}

static void on_proxy_connect(hio_t* proxyio) {
    hio_t* connio = (hio_t*)hevent_userdata(proxyio);
    assert(connio != NULL);
    hio_read(connio);

    hio_setcb_close(proxyio, on_proxy_close);
    hio_setcb_read(proxyio, on_proxy_recv);
    hio_read(proxyio);
}

static void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    hio_t* proxyio = (hio_t*)hevent_userdata(io);
    if (proxyio) {
        hevent_set_userdata(io, NULL);
        hio_close(proxyio);
    }
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("[%s] <=> [%s]\n",
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    printf("< %.*s", readbytes, (char*)buf);

    hio_t* proxyio = (hio_t*)hevent_userdata(io);
    assert(proxyio != NULL);
    hio_write(proxyio, buf, readbytes);
}

static void on_accept(hio_t* io) {
    printf("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));

    hio_t* proxyio = hloop_create_tcp_client(hevent_loop(io), proxy_host, proxy_port, on_proxy_connect);
    if (proxyio == NULL) {
        hio_close(io);
        return;
    }

    hio_setcb_read(io, on_recv);
    hio_setcb_close(io, on_close);
    hevent_set_userdata(io, proxyio);
    hevent_set_userdata(proxyio, io);
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
