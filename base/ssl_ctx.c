#include "ssl_ctx.h"

#include <stdio.h>

#ifdef WITH_OPENSSL
#include "openssl/ssl.h"
#endif

static void* s_ssl_ctx = 0;

int ssl_ctx_init(const char* crt_file, const char* key_file, const char* ca_file) {
#ifdef WITH_OPENSSL
    if (s_ssl_ctx != NULL) {
        return 0;
    }
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (ctx == NULL) return -10;
    if (ca_file && *ca_file) {
        if (!SSL_CTX_load_verify_locations(ctx, ca_file, NULL)) {
            fprintf(stderr, "ssl ca_file verify failed!\n");
            return -20;
        }
    }
    if (crt_file && *crt_file) {
        if (!SSL_CTX_use_certificate_file(ctx, crt_file, SSL_FILETYPE_PEM)) {
            fprintf(stderr, "ssl crt_file error!\n");
            return -20;
        }
    }
    if (key_file && *key_file) {
        if (!SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM)) {
            fprintf(stderr, "ssl key_file error!\n");
            return -30;
        }
        if (!SSL_CTX_check_private_key(ctx)) {
            fprintf(stderr, "ssl key_file check failed!\n");
            return -40;
        }
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    s_ssl_ctx = ctx;
    return 0;
#else
    fprintf(stderr, "Please recompile WITH_OPENSSL.\n");
    return -1;
#endif
}

int ssl_ctx_destory() {
#ifdef WITH_OPENSSL
    if (s_ssl_ctx) {
        SSL_CTX_free((SSL_CTX*)s_ssl_ctx);
        s_ssl_ctx = NULL;
    }
#endif
    return 0;
}

void* ssl_ctx_instance() {
    return s_ssl_ctx;
}
