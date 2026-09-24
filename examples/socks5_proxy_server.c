/*
 * SOCKS5 CONNECT proxy server.
 *
 * @build:        make socks5_proxy_server
 * @proxy_server: bin/socks5_proxy_server 1080
 *                bin/socks5_proxy_server 1080 username password
 * @proxy_client: curl -v http://www.example.com/ --proxy socks5://127.0.0.1:1080
 */

#include "hloop.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s proxy_port [username] [password]\n", argv[0]);
        return -10;
    }

    proxy_setting_t setting;
    memset(&setting, 0, sizeof(setting));
    strncpy(setting.proxy_host, "0.0.0.0", sizeof(setting.proxy_host) - 1);
    setting.proxy_port = atoi(argv[1]);
    if (argc > 2) {
        strncpy(setting.username, argv[2], sizeof(setting.username) - 1);
    }
    if (argc > 3) {
        strncpy(setting.password, argv[3], sizeof(setting.password) - 1);
    }

    hloop_t* loop = hloop_new(0);
    hio_t* listener = hio_create_socks5_proxy_server(loop, &setting);
    if (listener == NULL) {
        hloop_free(&loop);
        return -20;
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
