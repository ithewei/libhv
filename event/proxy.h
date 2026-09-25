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
bool proxy_setting_valid(const proxy_setting_t* setting, bool need_target);

// Build an HTTP CONNECT request for the configured target. Returns bytes
// written, or a negative value when the buffer is insufficient.
int http_connect_build_request(const proxy_ctx_t* proxy, char* buf, int bufsize);

void proxy_handshake_start(hio_t* io);
void proxy_handshake_read(hio_t* io);
void proxy_handshake_fail(hio_t* io);
int  proxy_handshake_write(hio_t* io, const void* buf, int len);
void proxy_handshake_established(hio_t* io);

#endif // HV_PROXY_H_
