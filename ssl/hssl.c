#include "hssl.h"

hssl_ctx_t g_ssl_ctx = NULL;

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    if (g_ssl_ctx == NULL) {
        g_ssl_ctx = hssl_ctx_new(param);
    }
    return g_ssl_ctx;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
    hssl_ctx_free(ssl_ctx);
    if (g_ssl_ctx == ssl_ctx) {
        g_ssl_ctx = NULL;
    }
}

hssl_ctx_t hssl_ctx_instance() {
    if (g_ssl_ctx == NULL) {
        g_ssl_ctx = hssl_ctx_new(NULL);
    }
    return g_ssl_ctx;
}
