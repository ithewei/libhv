#ifndef HV_SOCKS5_H_
#define HV_SOCKS5_H_

// SOCKS5 client proxy support (RFC 1928 + RFC 1929 username/password auth).
//
// This is used internally by hio_connect() when hio_set_socks5() has been
// called: instead of connecting to the target directly, the io connects to the
// SOCKS5 proxy and runs a CONNECT handshake to the original target (sent as a
// domain name so the proxy resolves it). See hio_set_socks5() in hloop.h.

#include "hexport.h"

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

// User-facing SOCKS5 proxy configuration (like unpack_setting_t /
// reconn_setting_t). Passed to hio_set_socks5(); the value is copied, so a
// stack variable is fine.
typedef struct socks5_setting_s {
    char host[256];     // proxy host
    int  port;          // proxy port
    char username[256]; // empty => no auth
    char password[256];

#ifdef __cplusplus
    socks5_setting_s() {
        host[0] = '\0';
        port = 0;
        username[0] = '\0';
        password[0] = '\0';
    }
#endif
} socks5_setting_t;

// Internal per-connection runtime state for the SOCKS5 handshake (held on
// hio_t). Not part of the public configuration.
typedef struct socks5_conn_s {
    socks5_setting_t setting;       // copied proxy config
    char target_host[256];          // address the proxy should CONNECT to
    int  target_port;
    int  state;                     // socks5_state_e (see nio.c)
} socks5_conn_t;

BEGIN_EXTERN_C

// Build SOCKS5 handshake messages into buf; return bytes written (<0 on error).
HV_EXPORT int socks5_build_method_request (const socks5_conn_t* s5, unsigned char* buf);
HV_EXPORT int socks5_build_auth_request   (const socks5_conn_t* s5, unsigned char* buf);
HV_EXPORT int socks5_build_connect_request(const socks5_conn_t* s5, unsigned char* buf);
// Expected CONNECT reply length for a fixed-size ATYP (ipv4/ipv6); -1 otherwise.
HV_EXPORT int socks5_connect_reply_len(unsigned char atyp);

END_EXTERN_C

#endif // HV_SOCKS5_H_
