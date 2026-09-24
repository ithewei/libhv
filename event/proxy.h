#ifndef HV_PROXY_H_
#define HV_PROXY_H_

#include "hloop.h"

// Internal context held by hio_t::proxy. ctx is available to either client or
// server proxy implementations; ctx_free, when set, owns its cleanup.
typedef struct proxy_conn_s {
    proxy_setting_t setting;
    void*           ctx;
    void            (*ctx_free)(void* ctx);
    int             state;
    unsigned char   rbuf[1024];
    int             rlen;
    int             want;
} proxy_conn_t;

proxy_conn_t* proxy_conn_dup(const proxy_conn_t* proxy);
void proxy_conn_free(proxy_conn_t* proxy);

#endif // HV_PROXY_H_
