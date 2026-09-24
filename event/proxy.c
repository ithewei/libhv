#include "proxy.h"

#include "hevent.h"
#include "socks5.h"

proxy_conn_t* proxy_conn_dup(const proxy_conn_t* proxy) {
    if (proxy == NULL) return NULL;
    proxy_conn_t* copy = NULL;
    HV_ALLOC_SIZEOF(copy);
    if (copy) {
        copy->setting = proxy->setting;
        copy->side = proxy->side;
        copy->server_type = proxy->server_type;
    }
    return copy;
}
void proxy_conn_free(proxy_conn_t* proxy) {
    if (proxy) {
        if (proxy->side == PROXY_SIDE_SERVER) socks5_server_ctx_free(proxy->server_ctx);
        HV_FREE(proxy);
    }
}

static bool proxy_server_setting_valid(const proxy_setting_t* setting, bool need_target) {
    if (setting == NULL || setting->proxy_host[0] == '\0' ||
        setting->proxy_port < 0 || setting->proxy_port > 65535) {
        return false;
    }
    return !need_target ||
           (setting->target_host[0] != '\0' &&
            setting->target_port > 0 && setting->target_port <= 65535);
}

static proxy_conn_t* proxy_server_new(const proxy_setting_t* setting, proxy_server_type_e type) {
    proxy_conn_t* proxy = NULL;
    HV_ALLOC_SIZEOF(proxy);
    if (proxy == NULL) return NULL;
    proxy->setting = *setting;
    proxy->side = PROXY_SIDE_SERVER;
    proxy->server_type = (unsigned char)type;
    return proxy;
}

static void on_tcp_proxy_accept(hio_t* io) {
    proxy_conn_t* proxy = io->proxy;
    if (proxy == NULL || hio_setup_tcp_upstream(io, proxy->setting.target_host,
                                                 proxy->setting.target_port, 0) == NULL) {
        hio_close(io);
    }
}

hio_t* hio_create_tcp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_server_setting_valid(setting, true)) return NULL;
    proxy_conn_t* proxy = proxy_server_new(setting, PROXY_SERVER_TCP);
    if (proxy == NULL) return NULL;
    hio_t* listener = hloop_create_tcp_server(loop, setting->proxy_host, setting->proxy_port, on_tcp_proxy_accept);
    if (listener == NULL) { proxy_conn_free(proxy); return NULL; }
    listener->proxy = proxy;
    return listener;
}

hio_t* hio_create_udp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_server_setting_valid(setting, true)) return NULL;
    proxy_conn_t* proxy = proxy_server_new(setting, PROXY_SERVER_UDP);
    if (proxy == NULL) return NULL;
    hio_t* listener = hloop_create_udp_server(loop, setting->proxy_host, setting->proxy_port);
    if (listener == NULL) { proxy_conn_free(proxy); return NULL; }
    listener->proxy = proxy;
    if (hio_setup_udp_upstream(listener, proxy->setting.target_host, proxy->setting.target_port) == NULL) {
        hio_close(listener);
        return NULL;
    }
    return listener;
}
