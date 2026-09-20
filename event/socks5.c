#include "socks5.h"

#include <string.h>

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

// Build a CONNECT request using ATYP=domain (the proxy resolves the target).
//   +----+-----+-------+------+----------+----------+
//   |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
//   +----+-----+-------+------+----------+----------+
// Returns bytes written, or -1 if the target host is too long.
int socks5_build_connect_request(const socks5_conn_t* s5, unsigned char* buf) {
    int hlen = (int)strlen(s5->target_host);
    if (hlen <= 0 || hlen > 255) return -1;
    int n = 0;
    buf[n++] = SOCKS5_VERSION;
    buf[n++] = SOCKS5_CMD_CONNECT;
    buf[n++] = 0x00;                     // RSV
    buf[n++] = SOCKS5_ATYP_DOMAIN;
    buf[n++] = (unsigned char)hlen;
    memcpy(buf + n, s5->target_host, hlen); n += hlen;
    unsigned short port = (unsigned short)s5->target_port;
    buf[n++] = (unsigned char)((port >> 8) & 0xFF);
    buf[n++] = (unsigned char)(port & 0xFF);
    return n;
}

// The CONNECT reply's bound-address section is variable-length by ATYP; return
// the total expected reply length for the given atyp, or -1 if unknown.
// Fixed part is 4 bytes (VER REP RSV ATYP) + addr + 2 (port).
int socks5_connect_reply_len(unsigned char atyp) {
    switch (atyp) {
    case SOCKS5_ATYP_IPV4:   return 4 + 4 + 2;
    case SOCKS5_ATYP_IPV6:   return 4 + 16 + 2;
    case SOCKS5_ATYP_DOMAIN: return -1;  // needs the length byte, handled by caller
    default:                 return -1;
    }
}
