#ifndef HV_PROXY_H_
#define HV_PROXY_H_

#include "hloop.h"

// Internal context held by hio_t::proxy. ctx is available to either client or
// server proxy implementations; ctx_free, when set, owns its cleanup.
typedef struct proxy_ctx_s {
    proxy_setting_t setting;
    void*           ctx;
    void            (*ctx_free)(void* ctx);
    int             state;
    unsigned char   rbuf[1024];
    int             rlen;
    int             want;
} proxy_ctx_t;

proxy_ctx_t* proxy_ctx_new(const proxy_setting_t* setting);
proxy_ctx_t* proxy_ctx_dup(const proxy_ctx_t* proxy);
void proxy_ctx_free(proxy_ctx_t* proxy);

#endif // HV_PROXY_H_
