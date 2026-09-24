#include "proxy.h"

#include "hevent.h"

proxy_ctx_t* proxy_ctx_new(const proxy_setting_t* setting) {
    if (setting == NULL) return NULL;
    proxy_ctx_t* proxy = NULL;
    HV_ALLOC_SIZEOF(proxy);
    if (proxy == NULL) return NULL;
    proxy->setting = *setting;
    return proxy;
}

proxy_ctx_t* proxy_ctx_dup(const proxy_ctx_t* proxy) {
    if (proxy == NULL) return NULL;
    proxy_ctx_t* copy = NULL;
    HV_ALLOC_SIZEOF(copy);
    if (copy) {
        copy->setting = proxy->setting;
        copy->ctx_free = proxy->ctx_free;
    }
    return copy;
}

void proxy_ctx_free(proxy_ctx_t* proxy) {
    if (proxy) {
        if (proxy->ctx && proxy->ctx_free) proxy->ctx_free(proxy->ctx);
        HV_FREE(proxy);
    }
}

bool proxy_setting_valid(const proxy_setting_t* setting, bool need_target) {
    if (setting == NULL || setting->proxy_host[0] == '\0' ||
        setting->proxy_port < 0 || setting->proxy_port > 65535) {
        return false;
    }
    return !need_target ||
           (setting->target_host[0] != '\0' &&
            setting->target_port > 0 && setting->target_port <= 65535);
}

static void on_tcp_proxy_accept(hio_t* io) {
    proxy_ctx_t* proxy = io->proxy;
    if (proxy == NULL || hio_setup_tcp_upstream(io, proxy->setting.target_host,
                                                 proxy->setting.target_port, 0) == NULL) {
        hio_close(io);
    }
}

hio_t* hio_create_tcp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_setting_valid(setting, true)) return NULL;
    proxy_ctx_t* proxy = proxy_ctx_new(setting);
    if (proxy == NULL) return NULL;
    hio_t* listener = hloop_create_tcp_server(loop, setting->proxy_host, setting->proxy_port, on_tcp_proxy_accept);
    if (listener == NULL) { proxy_ctx_free(proxy); return NULL; }
    listener->proxy = proxy;
    return listener;
}

hio_t* hio_create_udp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_setting_valid(setting, true)) return NULL;
    proxy_ctx_t* proxy = proxy_ctx_new(setting);
    if (proxy == NULL) return NULL;
    hio_t* listener = hloop_create_udp_server(loop, setting->proxy_host, setting->proxy_port);
    if (listener == NULL) { proxy_ctx_free(proxy); return NULL; }
    listener->proxy = proxy;
    if (hio_setup_udp_upstream(listener, proxy->setting.target_host, proxy->setting.target_port) == NULL) {
        hio_close(listener);
        return NULL;
    }
    return listener;
}
