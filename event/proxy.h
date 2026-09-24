#ifndef HV_PROXY_H_
#define HV_PROXY_H_

#include "hloop.h"

typedef enum {
    PROXY_SIDE_CLIENT,
    PROXY_SIDE_SERVER,
} proxy_side_e;

typedef enum {
    PROXY_SERVER_TCP,
    PROXY_SERVER_UDP,
    PROXY_SERVER_SOCKS5,
} proxy_server_type_e;

// Internal context held by hio_t::proxy. Client connections use the handshake
// fields; server listeners and accepted connections use side/server_type.
typedef struct proxy_conn_s {
    proxy_setting_t setting;
    proxy_side_e    side;
    unsigned char   server_type;
    void*           server_ctx;
    int             state;
    unsigned char   rbuf[1024];
    int             rlen;
    int             want;
} proxy_conn_t;

proxy_conn_t* proxy_conn_dup(const proxy_conn_t* proxy);
void proxy_conn_free(proxy_conn_t* proxy);

#endif // HV_PROXY_H_
