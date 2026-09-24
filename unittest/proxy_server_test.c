#include <assert.h>
#include <string.h>

#include "hloop.h"
#include "hsocket.h"
#include "proxy.h"

int main() {
    hloop_t* loop = hloop_new(0);
    assert(loop != NULL);
    assert(hloop_create_tcp_proxy_server(loop, NULL) == NULL);
    assert(hloop_create_udp_proxy_server(loop, NULL) == NULL);

    proxy_setting_t setting;
    memset(&setting, 0, sizeof(setting));
    strcpy(setting.proxy_host, "127.0.0.1");
    setting.proxy_port = 0;
    strcpy(setting.target_host, "127.0.0.1");
    setting.target_port = 1;

    assert(hloop_create_tcp_proxy_server(loop, &setting) != NULL);
    assert(hloop_create_udp_proxy_server(loop, &setting) != NULL);
    setting.target_host[0] = '\0';
    setting.target_port = 0;
    hio_t* socks5_server = hloop_create_socks5_proxy_server(loop, &setting);
    assert(socks5_server != NULL);

    strcpy(setting.target_host, "127.0.0.1");
    setting.target_port = 1234;
    setting.proxy_port = ntohs(((sockaddr_u*)hio_localaddr(socks5_server))->sin.sin_port);
    setting.protocol = PROXY_PROTOCOL_NONE;
    assert(hloop_create_socks5_client(loop, &setting, NULL, NULL) != NULL);

    proxy_ctx_t* proxy = proxy_ctx_new(&setting);
    assert(proxy != NULL);
    assert(proxy->on_established == NULL);
    proxy_ctx_free(proxy);
    hloop_free(&loop);
    return 0;
}
