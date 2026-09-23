#ifndef HV_SOCKS5_H_
#define HV_SOCKS5_H_

// Internal SOCKS5 client helpers (RFC 1928 + RFC 1929 username/password auth).
//
// NOTE: this is an internal header (not installed). The public API is
// proxy_setting_t + hio_set_proxy() in hloop.h. Used internally by
// hio_connect() to run the proxy handshake; see nio.c.

#include "hloop.h"    // proxy_setting_t

#define SOCKS5_VERSION          0x05
#define SOCKS5_AUTH_VERSION     0x01    // username/password auth subnegotiation

// auth methods
#define SOCKS5_AUTH_NONE        0x00
#define SOCKS5_AUTH_USERPASS    0x02
#define SOCKS5_AUTH_NOACCEPT    0xFF

// commands
#define SOCKS5_CMD_CONNECT      0x01

// address types
#define SOCKS5_ATYP_IPV4        0x01
#define SOCKS5_ATYP_DOMAIN      0x03
#define SOCKS5_ATYP_IPV6        0x04

// reply codes (0x00 = success)
#define SOCKS5_REP_SUCCESS      0x00

// Internal per-connection runtime state for the proxy handshake (held on
// hio_t). Not part of the public configuration. Shared by SOCKS5 and HTTP
// CONNECT.
typedef struct proxy_conn_s {
    proxy_setting_t setting;        // copied proxy config (target + auth)
    int  state;                     // socks5_state_e (see nio.c)
    // handshake read accumulator: replies may be fragmented across TCP
    // segments, so bytes are buffered here until a full message is available.
    // SOCKS5 max reply is small; HTTP CONNECT response headers can be larger.
    unsigned char rbuf[1024];
    int  rlen;                      // bytes currently in rbuf
    int  want;                      // bytes needed to complete the current step (SOCKS5)
} proxy_conn_t;

BEGIN_EXTERN_C

// Build SOCKS5 handshake messages into buf; return bytes written (<0 on error).
int socks5_build_method_request (const proxy_conn_t* s5, unsigned char* buf);
int socks5_build_auth_request   (const proxy_conn_t* s5, unsigned char* buf);
int socks5_build_connect_request(const proxy_conn_t* s5, unsigned char* buf);

// Build an HTTP CONNECT request into buf (size bufsize). Sends
//   CONNECT target_host:target_port HTTP/1.1
//   Host: target_host:target_port
//   [Proxy-Authorization: Basic base64(user:pass)]
//   (blank line)
// Returns bytes written (<0 on error / truncation).
int http_connect_build_request(const proxy_conn_t* p, char* buf, int bufsize);

END_EXTERN_C

#endif // HV_SOCKS5_H_
