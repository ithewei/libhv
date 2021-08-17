#include "hssl.h"

#ifdef WITH_OPENSSL

#include "openssl/ssl.h"
#include "openssl/err.h"
#ifdef _MSC_VER
//#pragma comment(lib, "libssl.a")
//#pragma comment(lib, "libcrypto.a")
#endif

const char* hssl_backend() {
    return "openssl";
}

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    static int s_initialized = 0;
    if (s_initialized == 0) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        SSL_library_init();
        SSL_load_error_strings();
#else
        OPENSSL_init_ssl(OPENSSL_INIT_SSL_DEFAULT, NULL);
#endif
        s_initialized = 1;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX* ctx = SSL_CTX_new(SSLv23_method());
#else
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
#endif
    if (ctx == NULL) return NULL;
    int mode = SSL_VERIFY_NONE;
    const char* ca_file = NULL;
    const char* ca_path = NULL;
    if (param) {
        if (param->ca_file && *param->ca_file) {
            ca_file = param->ca_file;
        }
        if (param->ca_path && *param->ca_path) {
            ca_path = param->ca_path;
        }
        if (ca_file || ca_path) {
            if (!SSL_CTX_load_verify_locations(ctx, ca_file, ca_path)) {
                fprintf(stderr, "ssl ca_file/ca_path failed!\n");
                goto error;
            }
        }

        if (param->crt_file && *param->crt_file) {
            if (!SSL_CTX_use_certificate_file(ctx, param->crt_file, SSL_FILETYPE_PEM)) {
                fprintf(stderr, "ssl crt_file error!\n");
                goto error;
            }
        }

        if (param->key_file && *param->key_file) {
            if (!SSL_CTX_use_PrivateKey_file(ctx, param->key_file, SSL_FILETYPE_PEM)) {
                fprintf(stderr, "ssl key_file error!\n");
                goto error;
            }
            if (!SSL_CTX_check_private_key(ctx)) {
                fprintf(stderr, "ssl key_file check failed!\n");
                goto error;
            }
        }

        if (param->verify_peer) {
            mode = SSL_VERIFY_PEER;
        }
    }
    if (mode == SSL_VERIFY_PEER && !ca_file && !ca_path) {
        SSL_CTX_set_default_verify_paths(ctx);
    }
    SSL_CTX_set_verify(ctx, mode, NULL);

    g_ssl_ctx = ctx;
    return ctx;
error:
    SSL_CTX_free(ctx);
    return NULL;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
    if (!ssl_ctx) return;
    if (g_ssl_ctx == ssl_ctx) {
        g_ssl_ctx = NULL;
    }
    SSL_CTX_free((SSL_CTX*)ssl_ctx);
}

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd) {
    SSL* ssl = SSL_new((SSL_CTX*)ssl_ctx);
    if (ssl == NULL) return NULL;
    SSL_set_fd(ssl, fd);
    return ssl;
}

void hssl_free(hssl_t ssl) {
    if (ssl) {
        SSL_free((SSL*)ssl);
        ssl = NULL;
    }
}

int hssl_accept(hssl_t ssl) {
    int ret = SSL_accept((SSL*)ssl);
    if (ret == 1) return 0;

    int err = SSL_get_error((SSL*)ssl, ret);
    if (err == SSL_ERROR_WANT_READ) {
        return HSSL_WANT_READ;
    }
    else if (err == SSL_ERROR_WANT_WRITE) {
        return HSSL_WANT_WRITE;
    }
    return err;
}

int hssl_connect(hssl_t ssl) {
    int ret = SSL_connect((SSL*)ssl);
    if (ret == 1) return 0;

    int err = SSL_get_error((SSL*)ssl, ret);
    if (err == SSL_ERROR_WANT_READ) {
        return HSSL_WANT_READ;
    }
    else if (err == SSL_ERROR_WANT_WRITE) {
        return HSSL_WANT_WRITE;
    }
    return err;
}

int hssl_read(hssl_t ssl, void* buf, int len) {
    return SSL_read((SSL*)ssl, buf, len);
}

int hssl_write(hssl_t ssl, const void* buf, int len) {
    return SSL_write((SSL*)ssl, buf, len);
}

int hssl_close(hssl_t ssl) {
    SSL_shutdown((SSL*)ssl);
    return 0;
}

int hssl_set_sni_hostname(hssl_t ssl, const char* hostname) {
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    SSL_set_tlsext_host_name((SSL*)ssl, hostname);
#endif
    return 0;
}

#endif // WITH_OPENSSL
