/*
 * udp proxy server
 *
 * @build:        make examples
 * @udp_server:   bin/udp_echo_server 1234
 * @proxy_server: bin/udp_proxy_server 2222 127.0.0.1:1234
 * @client:       bin/nc -u 127.0.0.1 2222
 *                > hello
 *                < hello
 */

#include "hloop.h"

static char proxy_host[64] = "0.0.0.0";
static int  proxy_port = 1080;

static char backend_host[64] = "127.0.0.1";
static int  backend_port = 80;

// hloop_create_udp_server -> hio_setup_udp_upstream

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s proxy_port backend_host:backend_port\n", argv[0]);
        return -10;
    }
    proxy_port = atoi(argv[1]);
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
    printf("%s:%d proxy %s:%d\n", proxy_host, proxy_port, backend_host, backend_port);

    hloop_t* loop = hloop_new(0);
    hio_t* io = hloop_create_udp_server(loop, proxy_host, proxy_port);
    if (io == NULL) {
        return -20;
    }
    hio_t* upstream_io = hio_setup_udp_upstream(io, backend_host, backend_port);
    if (upstream_io == NULL) {
        return -30;
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
