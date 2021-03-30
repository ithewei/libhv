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

static char proxy_host[64] = "127.0.0.1";
static int proxy_port = 1234;

// hloop_create_udp_server -> hio_setup_udp_upstream

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
    printf("proxy: [%s:%d]\n", proxy_host, proxy_port);

    hloop_t* loop = hloop_new(0);
    hio_t* io = hloop_create_udp_server(loop, "0.0.0.0", port);
    if (io == NULL) {
        return -20;
    }
    hio_t* upstream_io = hio_setup_udp_upstream(io, proxy_host, proxy_port);
    if (upstream_io == NULL) {
        return -30;
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
