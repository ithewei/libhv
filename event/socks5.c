#include "socks5.h"

#include <string.h>

#include "hsocket.h"    // is_ipv4 / is_ipv6 / inet_pton via hplatform

// Build the SOCKS5 method-selection request.
//   +----+----------+----------+
//   |VER | NMETHODS | METHODS  |
//   +----+----------+----------+
// Offers NONE, plus USERPASS when auth credentials are present.
// Returns the number of bytes written.
int socks5_build_method_request(const socks5_conn_t* s5, unsigned char* buf) {
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
int socks5_build_auth_request(const socks5_conn_t* s5, unsigned char* buf) {
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
int socks5_build_connect_request(const socks5_conn_t* s5, unsigned char* buf) {
    int n = 0;
    buf[n++] = SOCKS5_VERSION;
    buf[n++] = SOCKS5_CMD_CONNECT;
    buf[n++] = 0x00;                     // RSV

    struct in_addr  addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, s5->target_host, &addr4) == 1) {
        buf[n++] = SOCKS5_ATYP_IPV4;
        memcpy(buf + n, &addr4, 4); n += 4;
    } else if (inet_pton(AF_INET6, s5->target_host, &addr6) == 1) {
        buf[n++] = SOCKS5_ATYP_IPV6;
        memcpy(buf + n, &addr6, 16); n += 16;
    } else {
        int hlen = (int)strlen(s5->target_host);
        if (hlen <= 0 || hlen > 255) return -1;
        buf[n++] = SOCKS5_ATYP_DOMAIN;
        buf[n++] = (unsigned char)hlen;
        memcpy(buf + n, s5->target_host, hlen); n += hlen;
    }
    unsigned short port = (unsigned short)s5->target_port;
    buf[n++] = (unsigned char)((port >> 8) & 0xFF);
    buf[n++] = (unsigned char)(port & 0xFF);
    return n;
}
