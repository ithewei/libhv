#include <assert.h>
#include <string.h>

#include "hloop.h"

int main() {
    hloop_t* loop = hloop_new(0);
    assert(loop != NULL);
    assert(hio_create_tcp_proxy_server(loop, NULL) == NULL);
    assert(hio_create_udp_proxy_server(loop, NULL) == NULL);

    proxy_setting_t setting;
    memset(&setting, 0, sizeof(setting));
    strcpy(setting.proxy_host, "127.0.0.1");
    setting.proxy_port = 0;
    strcpy(setting.target_host, "127.0.0.1");
    setting.target_port = 1;

    assert(hio_create_tcp_proxy_server(loop, &setting) != NULL);
    assert(hio_create_udp_proxy_server(loop, &setting) != NULL);
    hloop_free(&loop);
    return 0;
}
