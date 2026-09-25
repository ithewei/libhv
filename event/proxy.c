#include "proxy.h"

#include <stdio.h>
#include <string.h>

#include "hevent.h"
#include "herr.h"
#include "hlog.h"
#include "hsocket.h"
#include "socks5.h"

static int proxy_base64_encode(const unsigned char* in, int len, char* out) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int n = 0, i = 0;
    while (i + 3 <= len) {
        unsigned v = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        out[n++] = tbl[(v >> 18) & 0x3F];
        out[n++] = tbl[(v >> 12) & 0x3F];
        out[n++] = tbl[(v >> 6) & 0x3F];
        out[n++] = tbl[v & 0x3F];
        i += 3;
    }
    int rem = len - i;
    if (rem == 1) {
        unsigned v = in[i] << 16;
        out[n++] = tbl[(v >> 18) & 0x3F];
        out[n++] = tbl[(v >> 12) & 0x3F];
        out[n++] = '=';
        out[n++] = '=';
    } else if (rem == 2) {
        unsigned v = (in[i] << 16) | (in[i+1] << 8);
        out[n++] = tbl[(v >> 18) & 0x3F];
        out[n++] = tbl[(v >> 12) & 0x3F];
        out[n++] = tbl[(v >> 6) & 0x3F];
        out[n++] = '=';
    }
    return n;
}

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

int http_connect_build_request(const proxy_ctx_t* proxy, char* buf, int bufsize) {
    const char* host = proxy->setting.target_host;
    int port = proxy->setting.target_port;
    char authority[300];
    if (is_ipv6(host)) {
        snprintf(authority, sizeof(authority), "[%s]:%d", host, port);
    } else {
        snprintf(authority, sizeof(authority), "%s:%d", host, port);
    }
    int n = 0;
    int r = snprintf(buf + n, bufsize - n,
                     "CONNECT %s HTTP/1.1\r\nHost: %s\r\n",
                     authority, authority);
    if (r < 0 || r >= bufsize - n) return -1;
    n += r;

    if (proxy->setting.username[0]) {
        char cred[520];
        int c = snprintf(cred, sizeof(cred), "%s:%s",
                         proxy->setting.username, proxy->setting.password);
        if (c < 0 || c >= (int)sizeof(cred)) return -1;
        char b64[768];
        int b = proxy_base64_encode((const unsigned char*)cred, c, b64);
        b64[b] = '\0';
        r = snprintf(buf + n, bufsize - n, "Proxy-Authorization: Basic %s\r\n", b64);
        if (r < 0 || r >= bufsize - n) return -1;
        n += r;
    }

    r = snprintf(buf + n, bufsize - n, "\r\n");
    if (r < 0 || r >= bufsize - n) return -1;
    return n + r;
}

void proxy_handshake_fail(hio_t* io) {
    if (io->error == 0) io->error = ERR_CONNECT;
    hlogw("connfd=%d proxy handshake error", io->fd);
    hio_close(io);
}

int proxy_handshake_send(hio_t* io, const void* buf, int len) {
    int flag = 0;
#ifdef MSG_NOSIGNAL
    flag |= MSG_NOSIGNAL;
#endif
    return send(io->fd, (const char*)buf, len, flag) == len ? 0 : -1;
}

void proxy_handshake_established(hio_t* io) {
    proxy_ctx_t* proxy = io->proxy;
    hio_del(io, HV_READ);
    if (proxy == NULL) {
        proxy_handshake_fail(io);
        return;
    }
    io->phase = HIO_PHASE_PROXY_ESTABLISHED;
}

static void http_connect_client_handshake(hio_t* io) {
    proxy_ctx_t* proxy = io->proxy;
    for (;;) {
        int cap = (int)sizeof(proxy->rbuf) - proxy->rlen;
        if (cap <= 0) { proxy_handshake_fail(io); return; }
        int n = recv(io->fd, (char*)proxy->rbuf + proxy->rlen, cap, MSG_PEEK);
        if (n == 0) { proxy_handshake_fail(io); return; }
        if (n < 0) {
            int err = socket_errno();
            if (err == EAGAIN || err == EINTR) return;
            io->error = err;
            proxy_handshake_fail(io);
            return;
        }
        int have = proxy->rlen + n;
        int start = proxy->rlen >= 3 ? proxy->rlen - 3 : 0;
        int term = -1;
        for (int i = start + 3; i < have; ++i) {
            if (proxy->rbuf[i-3]=='\r' && proxy->rbuf[i-2]=='\n' &&
                proxy->rbuf[i-1]=='\r' && proxy->rbuf[i]=='\n') { term = i; break; }
        }
        if (term < 0) {
            int got = recv(io->fd, (char*)proxy->rbuf + proxy->rlen, n, 0);
            if (got <= 0) { proxy_handshake_fail(io); return; }
            proxy->rlen += got;
            continue;
        }
        int header_len = term + 1;
        int to_drain = header_len - proxy->rlen;
        if (to_drain > 0) {
            int got = recv(io->fd, (char*)proxy->rbuf + proxy->rlen, to_drain, 0);
            if (got != to_drain) { proxy_handshake_fail(io); return; }
            proxy->rlen += got;
        }
        int code = 0;
        char* sp = (char*)memchr(proxy->rbuf, ' ', proxy->rlen);
        if (sp) code = atoi(sp + 1);
        if (code >= 200 && code < 300) {
            proxy_handshake_established(io);
        } else {
            hlogw("connfd=%d http proxy CONNECT failed: %d", io->fd, code);
            io->error = ERR_CONNECT;
            proxy_handshake_fail(io);
        }
        return;
    }
}

static void http_connect_client_start(hio_t* io) {
    char buf[2048];
    int n = http_connect_build_request(io->proxy, buf, (int)sizeof(buf));
    if (n < 0 || proxy_handshake_send(io, buf, n) != 0) {
        proxy_handshake_fail(io);
        return;
    }
    io->proxy->rlen = 0;
    hio_add(io, NULL, HV_READ);
}

void proxy_handshake_read(hio_t* io) {
    if (io->proxy == NULL) {
        proxy_handshake_fail(io);
        return;
    }
    switch (io->proxy->setting.protocol) {
    case PROXY_PROTOCOL_SOCKS5:
        socks5_client_handshake_read(io);
        return;
    case PROXY_PROTOCOL_HTTP_CONNECT:
        http_connect_client_handshake(io);
        return;
    default:
        proxy_handshake_fail(io);
        return;
    }
}

void proxy_handshake_start(hio_t* io) {
    proxy_ctx_t* proxy = io->proxy;
    if (proxy == NULL) {
        proxy_handshake_fail(io);
        return;
    }
    io->phase = HIO_PHASE_PROXY_HANDSHAKING;
    if (io->events & HV_WRITE) {
        hio_del(io, HV_WRITE);
    }
    switch (proxy->setting.protocol) {
    case PROXY_PROTOCOL_SOCKS5:
        socks5_client_handshake_start(io);
        return;
    case PROXY_PROTOCOL_HTTP_CONNECT:
        http_connect_client_start(io);
        return;
    default:
        io->error = ERR_INVALID_PARAM;
        proxy_handshake_fail(io);
        return;
    }
}

static void on_tcp_proxy_accept(hio_t* io) {
    proxy_ctx_t* proxy = io->proxy;
    if (proxy == NULL || hio_setup_tcp_upstream(io, proxy->setting.target_host,
                                                 proxy->setting.target_port, 0) == NULL) {
        hio_close(io);
    }
}

hio_t* hloop_create_tcp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_setting_valid(setting, true)) return NULL;
    hio_t* listener = hloop_create_tcp_server(loop, setting->proxy_host, setting->proxy_port, on_tcp_proxy_accept);
    if (listener == NULL) return NULL;
    if (hio_set_proxy(listener, setting) != 0) {
        hio_close(listener);
        return NULL;
    }
    return listener;
}

hio_t* hloop_create_udp_proxy_server(hloop_t* loop, const proxy_setting_t* setting) {
    if (loop == NULL || !proxy_setting_valid(setting, true)) return NULL;
    hio_t* listener = hloop_create_udp_server(loop, setting->proxy_host, setting->proxy_port);
    if (listener == NULL) return NULL;
    if (hio_set_proxy(listener, setting) != 0) {
        hio_close(listener);
        return NULL;
    }
    if (hio_setup_udp_upstream(listener, setting->target_host, setting->target_port) == NULL) {
        hio_close(listener);
        return NULL;
    }
    return listener;
}
