#ifndef HV_SSL_H_
#define HV_SSL_H_

#include "hexport.h"

#include "hplatform.h"
#if !defined(WITH_OPENSSL) &&   \
    !defined(WITH_GNUTLS)  &&   \
    !defined(WITH_MBEDTLS)
#ifdef OS_WIN
#define WITH_WINTLS
#elif defined(OS_DARWIN)
#define WITH_APPLETLS
#else
#define HV_WITHOUT_SSL
#endif
#endif

typedef void* hssl_ctx_t;   ///> SSL_CTX
typedef void* hssl_t;       ///> SSL

enum {
    HSSL_SERVER = 0,
    HSSL_CLIENT = 1,
};

enum {
    HSSL_OK = 0,
    HSSL_ERROR = -1,
    HSSL_WANT_READ = -2,
    HSSL_WANT_WRITE = -3,
    HSSL_WOULD_BLOCK = -4,
};

typedef struct {
    const char* crt_file;
    const char* key_file;
    const char* ca_file;
    const char* ca_path;
    short       verify_peer;
    short       endpoint; // HSSL_SERVER / HSSL_CLIENT
} hssl_ctx_init_param_t;

BEGIN_EXTERN_C

/*
const char* hssl_backend() {
#ifdef WITH_OPENSSL
    return "openssl";
#elif defined(WITH_GNUTLS)
    return "gnutls";
#elif defined(WITH_MBEDTLS)
    return "mbedtls";
#else
    return "nossl";
#endif
}
*/
HV_EXPORT const char* hssl_backend();
#define HV_WITH_SSL (strcmp(hssl_backend(), "nossl") != 0)

HV_EXPORT extern hssl_ctx_t g_ssl_ctx;
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

HV_EXPORT int hssl_set_sni_hostname(hssl_t ssl, const char* hostname);

END_EXTERN_C

#endif // HV_SSL_H_
