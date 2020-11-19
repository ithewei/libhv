#ifndef HV_SSL_H_
#define HV_SSL_H_

#include "hexport.h"

typedef void* hssl_ctx_t; ///> SSL_CTX
typedef void* hssl_t; ///> SSL

enum {
    HSSL_OK = 0,
    HSSL_WANT_READ = -2,
    HSSL_WANT_WRITE = -3,
};

typedef struct {
    const char* crt_file;
    const char* key_file;
    const char* ca_file;
    short       verify_peer;
    short       endpoint; // 0: server 1: client
} hssl_ctx_init_param_t;

BEGIN_EXTERN_C

HV_EXPORT hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param);
HV_EXPORT void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx);
HV_EXPORT hssl_ctx_t hssl_ctx_instance();

HV_EXPORT hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd);
HV_EXPORT void hssl_free(hssl_t ssl);

HV_EXPORT int hssl_accept(hssl_t ssl);
HV_EXPORT int hssl_connect(hssl_t ssl);
HV_EXPORT int hssl_read(hssl_t ssl, void* buf, int len);
HV_EXPORT int hssl_write(hssl_t ssl, const void* buf, int len);
HV_EXPORT int hssl_close(hssl_t ssl);

END_EXTERN_C

#endif // HV_SSL_H_
