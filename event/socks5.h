#ifndef HV_SOCKS5_H_
#define HV_SOCKS5_H_

// Internal SOCKS5 client helpers (RFC 1928 + RFC 1929 username/password auth).
//
// NOTE: this is an internal header (not installed). The public API is
// socks5_setting_t + hio_set_socks5() in hloop.h. Used internally by
// hio_connect() to run the proxy handshake; see nio.c.

#include "hloop.h"    // socks5_setting_t

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

// Internal per-connection runtime state for the SOCKS5 handshake (held on
// hio_t). Not part of the public configuration.
typedef struct socks5_conn_s {
    socks5_setting_t setting;       // copied proxy config
    char target_host[256];          // address the proxy should CONNECT to
    int  target_port;
    int  state;                     // socks5_state_e (see nio.c)
    // handshake read accumulator: SOCKS5 replies may be fragmented across TCP
    // segments, so bytes are buffered here until a full message is available.
    unsigned char rbuf[300];        // max reply: 4 + 1 + 255 + 2 (domain bind)
    int  rlen;                      // bytes currently in rbuf
    int  want;                      // bytes needed to complete the current step
} socks5_conn_t;

BEGIN_EXTERN_C

// Build SOCKS5 handshake messages into buf; return bytes written (<0 on error).
int socks5_build_method_request (const socks5_conn_t* s5, unsigned char* buf);
int socks5_build_auth_request   (const socks5_conn_t* s5, unsigned char* buf);
int socks5_build_connect_request(const socks5_conn_t* s5, unsigned char* buf);

END_EXTERN_C

#endif // HV_SOCKS5_H_
