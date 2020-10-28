#include "hssl.h"

static hssl_ctx_t s_ssl_ctx = 0;
hssl_ctx_t hssl_ctx_instance() {
    return s_ssl_ctx;
}

#ifdef WITH_OPENSSL

#include "openssl/ssl.h"

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (ctx == NULL) return NULL;
    int mode = SSL_VERIFY_NONE;
    if (param) {
        if (param->ca_file && *param->ca_file) {
            if (!SSL_CTX_load_verify_locations(ctx, param->ca_file, NULL)) {
                fprintf(stderr, "ssl ca_file verify failed!\n");
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
    SSL_CTX_set_verify(ctx, mode, NULL);
    s_ssl_ctx = ctx;
    return ctx;
error:
    SSL_CTX_free(ctx);
    return NULL;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
    if (ssl_ctx) {
        if (ssl_ctx == s_ssl_ctx) {
            s_ssl_ctx = NULL;
        }
        SSL_CTX_free((SSL_CTX*)ssl_ctx);
        ssl_ctx = NULL;
    }
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

#else

#include "hplatform.h"

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    fprintf(stderr, "Please recompile WITH_SSL.\n");
    return NULL;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
}

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd) {
    return (void*)(intptr_t)fd;
}

void hssl_free(hssl_t ssl) {
}

int hssl_accept(hssl_t ssl) {
    return 0;
}

int hssl_connect(hssl_t ssl) {
    return 0;
}

int hssl_read(hssl_t ssl, void* buf, int len) {
    int fd = (intptr_t)ssl;
    return read(fd, buf, len);
}

int hssl_write(hssl_t ssl, const void* buf, int len) {
    int fd = (intptr_t)ssl;
    return write(fd, buf, len);
}

int hssl_close(hssl_t ssl) {
    return 0;
}
#endif
