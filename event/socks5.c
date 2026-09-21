#include "socks5.h"

#include <string.h>
#include <stdio.h>

#include "hsocket.h"    // is_ipv4 / is_ipv6 / inet_pton via hplatform

// Minimal base64 encoder for the HTTP CONNECT Proxy-Authorization header.
// NOTE: implemented locally (not via util/base64.h) because the event layer
// must not depend on util/ (core builds only add -I. -Ibase -Issl -Ievent).
// Writes ceil(len/3)*4 bytes to out (no NUL terminator); returns bytes written.
static int socks5_base64_encode(const unsigned char* in, int len, char* out) {
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

// Build the SOCKS5 method-selection request.
//   +----+----------+----------+
//   |VER | NMETHODS | METHODS  |
//   +----+----------+----------+
// Offers NONE, plus USERPASS when auth credentials are present.
// Returns the number of bytes written.
int socks5_build_method_request(const proxy_conn_t* s5, unsigned char* buf) {
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
int socks5_build_auth_request(const proxy_conn_t* s5, unsigned char* buf) {
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
int socks5_build_connect_request(const proxy_conn_t* s5, unsigned char* buf) {
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

// Build an HTTP CONNECT request (RFC 7231 4.3.6). The request-target is the
// authority form "host:port"; a Basic Proxy-Authorization header is added when
// credentials are present.
int http_connect_build_request(const proxy_conn_t* p, char* buf, int bufsize) {
    const char* host = p->setting.target_host;
    int port = p->setting.target_port;
    int n = 0;
    int r = snprintf(buf + n, bufsize - n,
                     "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n",
                     host, port, host, port);
    if (r < 0 || r >= bufsize - n) return -1;
    n += r;

    if (p->setting.username[0]) {
        // credentials = "user:pass"
        char cred[520];
        int c = snprintf(cred, sizeof(cred), "%s:%s",
                         p->setting.username, p->setting.password);
        if (c < 0 || c >= (int)sizeof(cred)) return -1;
        char b64[768];
        int b = socks5_base64_encode((const unsigned char*)cred, c, b64);
        b64[b] = '\0';
        r = snprintf(buf + n, bufsize - n, "Proxy-Authorization: Basic %s\r\n", b64);
        if (r < 0 || r >= bufsize - n) return -1;
        n += r;
    }

    r = snprintf(buf + n, bufsize - n, "\r\n");
    if (r < 0 || r >= bufsize - n) return -1;
    n += r;
    return n;
}
