#include "hssl.h"

hssl_ctx_t g_ssl_ctx = NULL;

hssl_ctx_t hssl_ctx_instance() {
    if (g_ssl_ctx == NULL) {
        g_ssl_ctx = hssl_ctx_init(NULL);
    }
    return g_ssl_ctx;
}
