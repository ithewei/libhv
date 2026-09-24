#include "socks5.h"

#include <string.h>
#include <stdio.h>

#include "hsocket.h"    // is_ipv4 / is_ipv6 / inet_pton via hplatform
#include "hevent.h"
#include "hdns.h"

typedef enum {
    S5S_METHOD_HEAD, S5S_METHODS, S5S_AUTH_HEAD, S5S_AUTH_USER, S5S_AUTH_PASS_HEAD,
    S5S_AUTH_PASS, S5S_REQUEST, S5S_ADDR, S5S_DOMAIN, S5S_PORT, S5S_RESOLVING, S5S_UPSTREAM,
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

static void socks5_server_close(hio_t* io) {
    hio_close_upstream(io);
}

void socks5_server_ctx_free(void* ctx) {
    socks5_server_conn_t* conn = (socks5_server_conn_t*)ctx;
    if (conn) {
        if (conn->dns) hdns_cancel(conn->dns);
        HV_FREE(conn);
    }
}

static void socks5_server_reply(hio_t* io, unsigned char rep) {
    unsigned char reply[] = {SOCKS5_VERSION, rep, 0, SOCKS5_ATYP_IPV4, 0, 0, 0, 0, 0, 0};
    hio_write(io, reply, sizeof(reply));
}

static void socks5_server_upstream_connect(hio_t* upstream) {
    hio_t* io = hio_get_upstream(upstream);
    if (io == NULL || io->proxy == NULL || io->proxy->ctx == NULL) return;
    socks5_server_reply(io, SOCKS5_REP_SUCCESS);
    hio_setcb_read(io, hio_write_upstream);
    hio_setcb_read(upstream, hio_write_upstream);
    hio_setcb_close(io, socks5_server_close);
    hio_setcb_close(upstream, socks5_server_close);
    hio_read(io);
    hio_read(upstream);
}

static void socks5_server_connect(hio_t* io, socks5_server_conn_t* conn) {
    int sockfd = socket(conn->addr.sa.sa_family, SOCK_STREAM, 0);
    if (sockfd < 0) { socks5_server_reply(io, 1); hio_close(io); return; }
    hio_t* upstream = hio_get(hevent_loop(io), sockfd);
    hio_set_peeraddr(upstream, &conn->addr.sa, sockaddr_len(&conn->addr));
    hio_setup_upstream(io, upstream);
    hio_setcb_connect(upstream, socks5_server_upstream_connect);
    hio_setcb_close(upstream, socks5_server_close);
    conn->state = S5S_UPSTREAM;
    hio_connect(upstream);
}

static void socks5_server_auth_finish(hio_t* io, socks5_server_conn_t* conn) {
    proxy_ctx_t* proxy = io->proxy;
    bool ok = conn->username_len == (int)strlen(proxy->setting.username) &&
              conn->password_len == (int)strlen(proxy->setting.password) &&
              memcmp(conn->username, proxy->setting.username, conn->username_len) == 0 &&
              memcmp(conn->password, proxy->setting.password, conn->password_len) == 0;
    unsigned char reply[] = {SOCKS5_AUTH_VERSION, ok ? 0 : 1};
    hio_write(io, reply, sizeof(reply));
    if (!ok) { hio_close(io); return; }
    conn->state = S5S_REQUEST;
    hio_readbytes(io, 4);
}

static void socks5_server_dns(hdns_t* query, const hdns_result_t* result, void* userdata) {
    socks5_server_conn_t* conn = (socks5_server_conn_t*)userdata;
    (void)query;
    conn->dns = NULL;
    if (conn->state != S5S_RESOLVING || !hio_is_opened(conn->io)) return;
    if (result->status != HDNS_STATUS_OK || result->naddrs == 0) {
        socks5_server_reply(conn->io, 4);
        hio_close(conn->io);
        return;
    }
    conn->addr = result->addrs[0];
    sockaddr_set_port(&conn->addr, conn->target_port);
    socks5_server_connect(conn->io, conn);
}

static void socks5_server_read(hio_t* io, void* buf, int len) {
    socks5_server_conn_t* conn = io->proxy ? (socks5_server_conn_t*)io->proxy->ctx : NULL;
    proxy_ctx_t* proxy = io->proxy;
    unsigned char* data = (unsigned char*)buf;
    if (conn == NULL || proxy == NULL) { hio_close(io); return; }
    switch (conn->state) {
    case S5S_METHOD_HEAD:
        if (len != 2 || data[0] != SOCKS5_VERSION || data[1] == 0) { hio_close(io); return; }
        conn->username_len = data[1];
        conn->state = S5S_METHODS;
        hio_readbytes(io, conn->username_len);
        break;
    case S5S_METHODS: {
        bool need_auth = proxy->setting.username[0] != '\0';
        bool offered = false;
        for (int i = 0; i < len; ++i) {
            if (data[i] == (need_auth ? SOCKS5_AUTH_USERPASS : SOCKS5_AUTH_NONE)) offered = true;
        }
        unsigned char reply[] = {SOCKS5_VERSION, offered ? (need_auth ? SOCKS5_AUTH_USERPASS : SOCKS5_AUTH_NONE) : SOCKS5_AUTH_NOACCEPT};
        hio_write(io, reply, sizeof(reply));
        if (!offered) { hio_close(io); return; }
        conn->state = need_auth ? S5S_AUTH_HEAD : S5S_REQUEST;
        hio_readbytes(io, need_auth ? 2 : 4);
        break;
    }
    case S5S_AUTH_HEAD:
        if (len != 2 || data[0] != SOCKS5_AUTH_VERSION || data[1] == 0) { hio_close(io); return; }
        conn->username_len = data[1]; conn->state = S5S_AUTH_USER; hio_readbytes(io, conn->username_len); break;
    case S5S_AUTH_USER:
        memcpy(conn->username, data, len); conn->state = S5S_AUTH_PASS_HEAD; hio_readbytes(io, 1); break;
    case S5S_AUTH_PASS_HEAD:
        conn->password_len = data[0];
        if (conn->password_len == 0) { socks5_server_auth_finish(io, conn); break; }
        conn->state = S5S_AUTH_PASS; hio_readbytes(io, conn->password_len); break;
    case S5S_AUTH_PASS:
        memcpy(conn->password, data, len); socks5_server_auth_finish(io, conn); break;
    case S5S_REQUEST:
        if (len != 4 || data[0] != SOCKS5_VERSION || data[1] != SOCKS5_CMD_CONNECT) { socks5_server_reply(io, 7); hio_close(io); return; }
        conn->addr_type = data[3]; conn->state = S5S_ADDR;
        if (conn->addr_type == SOCKS5_ATYP_IPV4) hio_readbytes(io, 4);
        else if (conn->addr_type == SOCKS5_ATYP_IPV6) hio_readbytes(io, 16);
        else if (conn->addr_type == SOCKS5_ATYP_DOMAIN) hio_readbytes(io, 1);
        else { socks5_server_reply(io, 8); hio_close(io); }
        break;
    case S5S_ADDR:
        if (conn->addr_type == SOCKS5_ATYP_DOMAIN) {
            if (len != 1 || data[0] == 0) { socks5_server_reply(io, 8); hio_close(io); return; }
            conn->username_len = data[0]; conn->state = S5S_DOMAIN; hio_readbytes(io, conn->username_len); break;
        }
        if (conn->addr_type == SOCKS5_ATYP_IPV4) { conn->addr.sa.sa_family = AF_INET; memcpy(&conn->addr.sin.sin_addr, data, 4); }
        else if (conn->addr_type == SOCKS5_ATYP_IPV6) { conn->addr.sa.sa_family = AF_INET6; memcpy(&conn->addr.sin6.sin6_addr, data, 16); }
        else { socks5_server_reply(io, 8); hio_close(io); return; }
        conn->state = S5S_PORT; hio_readbytes(io, 2); break;
    case S5S_DOMAIN:
        if (len <= 0 || len >= (int)sizeof(conn->host)) { socks5_server_reply(io, 8); hio_close(io); return; }
        memcpy(conn->host, data, len); conn->host[len] = '\0';
        conn->state = S5S_PORT; hio_readbytes(io, 2); break;
    case S5S_PORT:
        if (len != 2) { socks5_server_reply(io, 1); hio_close(io); return; }
        conn->target_port = ((unsigned)data[0] << 8) | data[1];
        if (conn->addr_type == SOCKS5_ATYP_DOMAIN) {
            conn->state = S5S_RESOLVING;
            conn->dns = hdns_resolve(hevent_loop(io), conn->host, socks5_server_dns, conn);
            if (conn->dns == NULL) { socks5_server_reply(io, 4); hio_close(io); }
        } else {
            sockaddr_set_port(&conn->addr, conn->target_port);
            socks5_server_connect(io, conn);
        }
        break;
    default: break;
    }
}

static void socks5_server_accept(hio_t* io) {
    socks5_server_conn_t* conn = NULL;
    HV_ALLOC_SIZEOF(conn);
    if (conn == NULL) { hio_close(io); return; }
    io->proxy->ctx = conn;
    conn->io = io;
    conn->state = S5S_METHOD_HEAD;
    hio_setcb_read(io, socks5_server_read);
    hio_setcb_close(io, socks5_server_close);
    hio_readbytes(io, 2);
}

// Build the SOCKS5 method-selection request.
//   +----+----------+----------+
//   |VER | NMETHODS | METHODS  |
//   +----+----------+----------+
// Offers NONE, plus USERPASS when auth credentials are present.
// Returns the number of bytes written.
int socks5_build_method_request(const proxy_ctx_t* s5, unsigned char* buf) {
    int n = 0;
    buf[n++] = SOCKS5_VERSION;
    if (s5->setting.username[0]) {
        buf[n++] = 2;                    // 2 methods
        buf[n++] = SOCKS5_AUTH_NONE;
        buf[n++] = SOCKS5_AUTH_USERPASS;
    } else {
        buf[n++] = 1;                    // 1 method
        buf[n++] = SOCKS5_AUTH_NONE;
    }
    return n;
}

// Build the RFC 1929 username/password auth request.
//   +----+------+----------+------+----------+
//   |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
//   +----+------+----------+------+----------+
int socks5_build_auth_request(const proxy_ctx_t* s5, unsigned char* buf) {
    int n = 0;
    int ulen = (int)strlen(s5->setting.username);
    int plen = (int)strlen(s5->setting.password);
    buf[n++] = SOCKS5_AUTH_VERSION;
    buf[n++] = (unsigned char)ulen;
    memcpy(buf + n, s5->setting.username, ulen); n += ulen;
    buf[n++] = (unsigned char)plen;
    memcpy(buf + n, s5->setting.password, plen); n += plen;
    return n;
}

// Build a CONNECT request.
//   +----+-----+-------+------+----------+----------+
//   |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
//   +----+-----+-------+------+----------+----------+
// An IPv4/IPv6 literal target is encoded as ATYP=1/4 (raw address bytes, per
// RFC 1928); anything else is sent as ATYP=domain so the proxy resolves it.
// Returns bytes written, or -1 on error (host too long).
int socks5_build_connect_request(const proxy_ctx_t* s5, unsigned char* buf) {
    int n = 0;
    buf[n++] = SOCKS5_VERSION;
    buf[n++] = SOCKS5_CMD_CONNECT;
    buf[n++] = 0x00;                     // RSV

    const char* target_host = s5->setting.target_host;
    struct in_addr  addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, target_host, &addr4) == 1) {
        buf[n++] = SOCKS5_ATYP_IPV4;
        memcpy(buf + n, &addr4, 4); n += 4;
    } else if (inet_pton(AF_INET6, target_host, &addr6) == 1) {
        buf[n++] = SOCKS5_ATYP_IPV6;
        memcpy(buf + n, &addr6, 16); n += 16;
    } else {
        int hlen = (int)strlen(target_host);
        if (hlen <= 0 || hlen > 255) return -1;
        buf[n++] = SOCKS5_ATYP_DOMAIN;
        buf[n++] = (unsigned char)hlen;
        memcpy(buf + n, target_host, hlen); n += hlen;
    }
    unsigned short port = (unsigned short)s5->setting.target_port;
    buf[n++] = (unsigned char)((port >> 8) & 0xFF);
    buf[n++] = (unsigned char)(port & 0xFF);
    return n;
}

hio_t* hio_create_socks5_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_setting_valid(setting, false)) {
        return NULL;
    }

    proxy_ctx_t* proxy = proxy_ctx_new(setting);
    if (proxy == NULL) return NULL;
    proxy->ctx_free = socks5_server_ctx_free;

    hio_t* listener = hloop_create_tcp_server(loop, setting->proxy_host,
                                               setting->proxy_port, socks5_server_accept);
    if (listener == NULL) {
        proxy_ctx_free(proxy);
        return NULL;
    }
    listener->proxy = proxy;
    return listener;
}
