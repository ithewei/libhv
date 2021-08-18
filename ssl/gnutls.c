#include "hssl.h"

#ifdef WITH_GNUTLS

#include "gnutls/gnutls.h"

const char* hssl_backend() {
    return "gnutls";
}

typedef gnutls_certificate_credentials_t gnutls_ctx_t;

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    static int s_initialized = 0;
    if (s_initialized == 0) {
        gnutls_global_init();
        s_initialized = 1;
    }

    gnutls_ctx_t ctx;
    const char* crt_file = NULL;
    const char* key_file = NULL;
    const char* ca_file = NULL;
    const char* ca_path = NULL;

    int ret = gnutls_certificate_allocate_credentials(&ctx);
    if (ret != GNUTLS_E_SUCCESS) {
        return NULL;
    }

    if (param) {
        if (param->crt_file && *param->crt_file) {
            crt_file = param->crt_file;
        }
        if (param->key_file && *param->key_file) {
            key_file = param->key_file;
        }
        if (param->ca_file && *param->ca_file) {
            ca_file = param->ca_file;
        }
        if (param->ca_path && *param->ca_path) {
            ca_path = param->ca_path;
        }

        if (ca_file) {
            ret = gnutls_certificate_set_x509_trust_file(ctx, ca_file, GNUTLS_X509_FMT_PEM);
            if (ret < 0) {
                fprintf(stderr, "ssl ca_file failed!\n");
                goto error;
            }
        }

        if (ca_path) {
            ret = gnutls_certificate_set_x509_trust_dir(ctx, ca_path, GNUTLS_X509_FMT_PEM);
            if (ret < 0) {
                fprintf(stderr, "ssl ca_path failed!\n");
                goto error;
            }
        }

        if (crt_file && key_file) {
            ret = gnutls_certificate_set_x509_key_file(ctx, crt_file, key_file, GNUTLS_X509_FMT_PEM);
            if (ret != GNUTLS_E_SUCCESS) {
                fprintf(stderr, "ssl crt_file/key_file error!\n");
                goto error;
            }
        }

        if (param->verify_peer && !ca_file && !ca_path) {
            gnutls_certificate_set_x509_system_trust(ctx);
        }
    }

    g_ssl_ctx = ctx;
    return ctx;
error:
    gnutls_certificate_free_credentials(ctx);
    return NULL;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
    if (!ssl_ctx) return;
    if (g_ssl_ctx == ssl_ctx) {
        g_ssl_ctx = NULL;
    }
    gnutls_ctx_t ctx = (gnutls_ctx_t)ssl_ctx;
    gnutls_certificate_free_credentials(ctx);
}

typedef struct gnutls_s {
    gnutls_session_t session;
    gnutls_ctx_t     ctx;
    int              fd;
} gnutls_t;

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd) {
    gnutls_t* gnutls = (gnutls_t*)malloc(sizeof(gnutls_t));
    if (gnutls == NULL) return NULL;
    gnutls->session = NULL;
    gnutls->ctx = (gnutls_ctx_t)ssl_ctx;
    gnutls->fd = fd;
    return (hssl_t)gnutls;
}

static int hssl_init(hssl_t ssl, int endpoint) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) {
        gnutls_init(&gnutls->session, endpoint);
        gnutls_priority_set_direct(gnutls->session, "NORMAL", NULL);
        gnutls_credentials_set(gnutls->session, GNUTLS_CRD_CERTIFICATE, gnutls->ctx);
        gnutls_transport_set_ptr(gnutls->session, (gnutls_transport_ptr_t)(ptrdiff_t)gnutls->fd);
    }
    return HSSL_OK;
}

void hssl_free(hssl_t ssl) {
    if (ssl == NULL) return;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session) {
        gnutls_deinit(gnutls->session);
        gnutls->session = NULL;
    }
    free(gnutls);
}

static int hssl_handshake(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) return HSSL_ERROR;
    int ret = 0;
    while (1) {
        ret = gnutls_handshake(gnutls->session);
        if (ret == GNUTLS_E_SUCCESS) {
            return HSSL_OK;
        }
        else if (ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED) {
            return gnutls_record_get_direction(gnutls->session) == 0 ? HSSL_WANT_READ : HSSL_WANT_WRITE;
        }
        else if (gnutls_error_is_fatal(ret)) {
            // fprintf(stderr, "gnutls_handshake failed: %s\n", gnutls_strerror(ret));
            return HSSL_ERROR;
        }
    }
    return HSSL_OK;
}

int hssl_accept(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) {
        hssl_init(ssl, GNUTLS_SERVER);
    }
    return hssl_handshake(ssl);
}

int hssl_connect(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) {
        hssl_init(ssl, GNUTLS_CLIENT);
    }
    return hssl_handshake(ssl);
}

int hssl_read(hssl_t ssl, void* buf, int len) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) return HSSL_ERROR;
    int ret = 0;
    while ((ret = gnutls_record_recv(gnutls->session, buf, len)) == GNUTLS_E_INTERRUPTED);
    return ret;
}

int hssl_write(hssl_t ssl, const void* buf, int len) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) return HSSL_ERROR;
    int ret = 0;
    while ((ret = gnutls_record_send(gnutls->session, buf, len)) == GNUTLS_E_INTERRUPTED);
    return ret;
}

int hssl_close(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) return HSSL_ERROR;
    gnutls_bye(gnutls->session, GNUTLS_SHUT_RDWR);
    return HSSL_OK;
}

int hssl_set_sni_hostname(hssl_t ssl, const char* hostname) {
    if (ssl == NULL) return HSSL_ERROR;
    gnutls_t* gnutls = (gnutls_t*)ssl;
    if (gnutls->session == NULL) {
        hssl_init(ssl, GNUTLS_CLIENT);
    }
    gnutls_server_name_set(gnutls->session, GNUTLS_NAME_DNS, hostname, strlen(hostname));
    return 0;
}

#endif // WITH_GNUTLS
