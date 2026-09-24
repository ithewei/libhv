/*
 * Fixed-upstream TCP proxy server.
 *
 * @build:        make tcp_proxy_server
 * @proxy_server: bin/tcp_proxy_server 1080 127.0.0.1:8080
 * @client:       curl -v http://127.0.0.1:1080/
 */

#include "hloop.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s proxy_port backend_host:backend_port\n", argv[0]);
        return -10;
    }

    proxy_setting_t setting;
    memset(&setting, 0, sizeof(setting));
    strncpy(setting.proxy_host, "0.0.0.0", sizeof(setting.proxy_host) - 1);
    setting.proxy_port = atoi(argv[1]);

    const char* target = argv[2];
    const char* colon = strrchr(target, ':');
    if (colon) {
        size_t host_len = (size_t)(colon - target);
        if (host_len >= sizeof(setting.target_host)) return -10;
        memcpy(setting.target_host, target, host_len);
        setting.target_host[host_len] = '\0';
        setting.target_port = atoi(colon + 1);
    } else {
        strncpy(setting.target_host, target, sizeof(setting.target_host) - 1);
        setting.target_port = 80;
    }

    hloop_t* loop = hloop_new(0);
    hio_t* listener = hio_create_tcp_proxy_server(loop, &setting);
    if (listener == NULL) {
        hloop_free(&loop);
        return -20;
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
