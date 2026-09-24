#ifndef HV_SOCKS5_H_
#define HV_SOCKS5_H_

// Internal SOCKS5 client helpers (RFC 1928 + RFC 1929 username/password auth).
//
// NOTE: this is an internal header (not installed). The public API is
// proxy_setting_t + hio_set_proxy() in hloop.h. Used internally by
// hio_connect() to run the proxy handshake; see nio.c.

#include "proxy.h"

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

BEGIN_EXTERN_C

// Build SOCKS5 handshake messages into buf; return bytes written (<0 on error).
int socks5_build_method_request (const proxy_ctx_t* s5, unsigned char* buf);
int socks5_build_auth_request   (const proxy_ctx_t* s5, unsigned char* buf);
int socks5_build_connect_request(const proxy_ctx_t* s5, unsigned char* buf);

// Build an HTTP CONNECT request into buf (size bufsize). Sends
//   CONNECT target_host:target_port HTTP/1.1
//   Host: target_host:target_port
//   [Proxy-Authorization: Basic base64(user:pass)]
//   (blank line)
// Returns bytes written (<0 on error / truncation).
int http_connect_build_request(const proxy_ctx_t* p, char* buf, int bufsize);

// Server-side SOCKS5 context lifecycle (implemented in socks5.c).
void socks5_server_ctx_free(void* ctx);

END_EXTERN_C

#endif // HV_SOCKS5_H_
