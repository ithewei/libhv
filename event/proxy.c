#include "proxy.h"

#include <string.h>

#include "hevent.h"
#include "hdns.h"
#include "socks5.h"

typedef enum {
    S5_METHOD_HEAD, S5_METHODS, S5_AUTH_HEAD, S5_AUTH_USER, S5_AUTH_PASS_HEAD,
    S5_AUTH_PASS, S5_REQUEST, S5_ADDR, S5_DOMAIN, S5_PORT, S5_RESOLVING, S5_UPSTREAM,
} socks5_server_state_e;

typedef struct {
    socks5_server_state_e state;
    unsigned char         addr_type;
    unsigned char         username[256];
    unsigned char         password[256];
    int                   username_len;
    int                   password_len;
    int                   target_port;
    sockaddr_u            addr;
    char                  host[256];
    hdns_t*               dns;
    hio_t*                io;
} socks5_server_conn_t;

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
        if (proxy->side == PROXY_SIDE_SERVER) {
            socks5_server_conn_t* conn = (socks5_server_conn_t*)proxy->server_ctx;
            if (conn) {
                if (conn->dns) hdns_cancel(conn->dns);
                HV_FREE(conn);
            }
        }
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

static void socks5_close(hio_t* io) {
    hio_close_upstream(io);
}

static void socks5_reply(hio_t* io, unsigned char rep) {
    unsigned char reply[] = {SOCKS5_VERSION, rep, 0, SOCKS5_ATYP_IPV4, 0, 0, 0, 0, 0, 0};
    hio_write(io, reply, sizeof(reply));
}

static void socks5_on_upstream_connect(hio_t* upstream) {
    hio_t* io = hio_get_upstream(upstream);
    if (io == NULL || io->proxy == NULL || io->proxy->server_ctx == NULL) return;
    socks5_reply(io, 0);
    hio_setcb_read(io, hio_write_upstream);
    hio_setcb_read(upstream, hio_write_upstream);
    hio_setcb_close(io, socks5_close);
    hio_setcb_close(upstream, socks5_close);
    hio_read(io);
    hio_read(upstream);
}

static void socks5_connect(hio_t* io, socks5_server_conn_t* conn) {
    int sockfd = socket(conn->addr.sa.sa_family, SOCK_STREAM, 0);
    if (sockfd < 0) { socks5_reply(io, 1); hio_close(io); return; }
    hio_t* upstream = hio_get(hevent_loop(io), sockfd);
    hio_set_peeraddr(upstream, &conn->addr.sa, sockaddr_len(&conn->addr));
    hio_setup_upstream(io, upstream);
    hio_setcb_connect(upstream, socks5_on_upstream_connect);
    hio_setcb_close(upstream, socks5_close);
    conn->state = S5_UPSTREAM;
    hio_connect(upstream);
}

static void socks5_auth_finish(hio_t* io, socks5_server_conn_t* conn) {
    proxy_conn_t* proxy = io->proxy;
    bool ok = conn->username_len == (int)strlen(proxy->setting.username) &&
              conn->password_len == (int)strlen(proxy->setting.password) &&
              memcmp(conn->username, proxy->setting.username, conn->username_len) == 0 &&
              memcmp(conn->password, proxy->setting.password, conn->password_len) == 0;
    unsigned char reply[] = {SOCKS5_AUTH_VERSION, ok ? 0 : 1};
    hio_write(io, reply, sizeof(reply));
    if (!ok) { hio_close(io); return; }
    conn->state = S5_REQUEST;
    hio_readbytes(io, 4);
}

static void socks5_on_dns(hdns_t* query, const hdns_result_t* result, void* userdata) {
    socks5_server_conn_t* conn = (socks5_server_conn_t*)userdata;
    (void)query;
    conn->dns = NULL;
    if (conn->state != S5_RESOLVING || !hio_is_opened(conn->io)) return;
    if (result->status != HDNS_STATUS_OK || result->naddrs == 0) {
        socks5_reply(conn->io, 4);
        hio_close(conn->io);
        return;
    }
    conn->addr = result->addrs[0];
    sockaddr_set_port(&conn->addr, conn->target_port);
    socks5_connect(conn->io, conn);
}

static void socks5_on_read(hio_t* io, void* buf, int len) {
    socks5_server_conn_t* conn = io->proxy ? (socks5_server_conn_t*)io->proxy->server_ctx : NULL;
    proxy_conn_t* proxy = io->proxy;
    unsigned char* data = (unsigned char*)buf;
    if (conn == NULL || proxy == NULL) { hio_close(io); return; }
    switch (conn->state) {
    case S5_METHOD_HEAD:
        if (len != 2 || data[0] != SOCKS5_VERSION || data[1] == 0) { hio_close(io); return; }
        conn->username_len = data[1];
        conn->state = S5_METHODS;
        hio_readbytes(io, conn->username_len);
        break;
    case S5_METHODS: {
        bool need_auth = proxy->setting.username[0] != '\0';
        bool offered = false;
        for (int i = 0; i < len; ++i) {
            if (data[i] == (need_auth ? SOCKS5_AUTH_USERPASS : SOCKS5_AUTH_NONE)) offered = true;
        }
        unsigned char reply[] = {SOCKS5_VERSION, offered ? (need_auth ? SOCKS5_AUTH_USERPASS : SOCKS5_AUTH_NONE) : SOCKS5_AUTH_NOACCEPT};
        hio_write(io, reply, sizeof(reply));
        if (!offered) { hio_close(io); return; }
        conn->state = need_auth ? S5_AUTH_HEAD : S5_REQUEST;
        hio_readbytes(io, need_auth ? 2 : 4);
        break;
    }
    case S5_AUTH_HEAD:
        if (len != 2 || data[0] != SOCKS5_AUTH_VERSION || data[1] == 0) { hio_close(io); return; }
        conn->username_len = data[1]; conn->state = S5_AUTH_USER; hio_readbytes(io, conn->username_len); break;
    case S5_AUTH_USER:
        memcpy(conn->username, data, len); conn->state = S5_AUTH_PASS_HEAD; hio_readbytes(io, 1); break;
    case S5_AUTH_PASS_HEAD:
        conn->password_len = data[0];
        if (conn->password_len == 0) { socks5_auth_finish(io, conn); break; }
        conn->state = S5_AUTH_PASS; hio_readbytes(io, conn->password_len); break;
    case S5_AUTH_PASS:
        memcpy(conn->password, data, len); socks5_auth_finish(io, conn); break;
    case S5_REQUEST:
        if (len != 4 || data[0] != SOCKS5_VERSION || data[1] != SOCKS5_CMD_CONNECT) { socks5_reply(io, 7); hio_close(io); return; }
        conn->addr_type = data[3]; conn->state = S5_ADDR;
        if (conn->addr_type == SOCKS5_ATYP_IPV4) hio_readbytes(io, 4);
        else if (conn->addr_type == SOCKS5_ATYP_IPV6) hio_readbytes(io, 16);
        else if (conn->addr_type == SOCKS5_ATYP_DOMAIN) hio_readbytes(io, 1);
        else { socks5_reply(io, 8); hio_close(io); }
        break;
    case S5_ADDR:
        if (conn->addr_type == SOCKS5_ATYP_DOMAIN) {
            if (len != 1 || data[0] == 0) { socks5_reply(io, 8); hio_close(io); return; }
            conn->username_len = data[0]; conn->state = S5_DOMAIN; hio_readbytes(io, conn->username_len); break;
        }
        if (conn->addr_type == SOCKS5_ATYP_IPV4) { conn->addr.sa.sa_family = AF_INET; memcpy(&conn->addr.sin.sin_addr, data, 4); }
        else if (conn->addr_type == SOCKS5_ATYP_IPV6) { conn->addr.sa.sa_family = AF_INET6; memcpy(&conn->addr.sin6.sin6_addr, data, 16); }
        else { socks5_reply(io, 8); hio_close(io); return; }
        conn->state = S5_PORT; hio_readbytes(io, 2); break;
    case S5_DOMAIN:
        if (len <= 0 || len >= (int)sizeof(conn->host)) { socks5_reply(io, 8); hio_close(io); return; }
        memcpy(conn->host, data, len); conn->host[len] = '\0';
        conn->state = S5_PORT; hio_readbytes(io, 2); break;
    case S5_PORT:
        if (len != 2) { socks5_reply(io, 1); hio_close(io); return; }
        conn->target_port = ((unsigned)data[0] << 8) | data[1];
        if (conn->addr_type == SOCKS5_ATYP_DOMAIN) {
            conn->state = S5_RESOLVING;
            conn->dns = hdns_resolve(hevent_loop(io), conn->host, socks5_on_dns, conn);
            if (conn->dns == NULL) { socks5_reply(io, 4); hio_close(io); }
        } else {
            sockaddr_set_port(&conn->addr, conn->target_port);
            socks5_connect(io, conn);
        }
        break;
    default: break;
    }
}

static void on_socks5_proxy_accept(hio_t* io) {
    socks5_server_conn_t* conn = NULL;
    HV_ALLOC_SIZEOF(conn);
    if (conn == NULL) { hio_close(io); return; }
    io->proxy->server_ctx = conn;
    conn->io = io;
    conn->state = S5_METHOD_HEAD;
    hio_setcb_read(io, socks5_on_read);
    hio_setcb_close(io, socks5_close);
    hio_readbytes(io, 2);
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

hio_t* hio_create_socks5_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_server_setting_valid(setting, false)) return NULL;
    proxy_conn_t* proxy = proxy_server_new(setting, PROXY_SERVER_SOCKS5);
    if (proxy == NULL) return NULL;
    hio_t* listener = hloop_create_tcp_server(loop, setting->proxy_host, setting->proxy_port, on_socks5_proxy_accept);
    if (listener == NULL) { proxy_conn_free(proxy); return NULL; }
    listener->proxy = proxy;
    return listener;
}
