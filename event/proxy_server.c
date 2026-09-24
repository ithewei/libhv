#include "proxy_server.h"

#include <string.h>

#include "hevent.h"

proxy_server_t* proxy_server_retain(proxy_server_t* server) {
    if (server) {
        ++server->refcount;
    }
    return server;
}

void proxy_server_release(proxy_server_t* server) {
    if (server && --server->refcount == 0) {
        HV_FREE(server);
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

static proxy_server_t* proxy_server_new(const proxy_setting_t* setting, proxy_server_type_e type) {
    proxy_server_t* server = NULL;
    HV_ALLOC_SIZEOF(server);
    if (server == NULL) return NULL;
    server->setting = *setting;
    server->type = type;
    server->refcount = 1;
    return server;
}

static void on_tcp_proxy_accept(hio_t* io) {
    proxy_server_t* server = io->proxy_server;
    if (server == NULL || hio_setup_tcp_upstream(io, server->setting.target_host,
                                                  server->setting.target_port, 0) == NULL) {
        hio_close(io);
    }
}

hio_t* hio_create_tcp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_server_setting_valid(setting, true)) return NULL;
    proxy_server_t* server = proxy_server_new(setting, PROXY_SERVER_TCP);
    if (server == NULL) return NULL;

    hio_t* listener = hloop_create_tcp_server(loop, setting->proxy_host,
                                               setting->proxy_port, on_tcp_proxy_accept);
    if (listener == NULL) {
        proxy_server_release(server);
        return NULL;
    }
    listener->proxy_server = server;
    return listener;
}

hio_t* hio_create_udp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_server_setting_valid(setting, true)) return NULL;
    proxy_server_t* server = proxy_server_new(setting, PROXY_SERVER_UDP);
    if (server == NULL) return NULL;

    hio_t* listener = hloop_create_udp_server(loop, setting->proxy_host, setting->proxy_port);
    if (listener == NULL) {
        proxy_server_release(server);
        return NULL;
    }
    listener->proxy_server = server;
    if (hio_setup_udp_upstream(listener, server->setting.target_host, server->setting.target_port) == NULL) {
        hio_close(listener);
        return NULL;
    }
    return listener;
}
