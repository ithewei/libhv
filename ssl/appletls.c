#include "hssl.h"

#ifdef WITH_APPLETLS

#include <Security/Security.h>
#include <Security/SecureTransport.h>
#include <CoreFoundation/CoreFoundation.h>

const char* hssl_backend() {
    return "appletls";
}

typedef struct appletls_ctx {
    SecIdentityRef cert;
    hssl_ctx_init_param_t* param;
} appletls_ctx_t;

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    appletls_ctx_t* ctx = (appletls_ctx_t*)malloc(sizeof(appletls_ctx_t));
    if (ctx == NULL) return NULL;
    ctx->cert = NULL;
    ctx->param = param;
    g_ssl_ctx = ctx;
    return ctx;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
    if (ssl_ctx == NULL) return;
    appletls_ctx_t* ctx = (appletls_ctx_t*)ssl_ctx;
    if (ctx->cert) {
        CFRelease(ctx->cert);
        ctx->cert = NULL;
    }
    free(ctx);
}

typedef struct appletls_s {
    SSLContextRef session;
    appletls_ctx_t* ctx;
    int fd;
} appletls_t;

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd) {
    if (ssl_ctx == NULL) return NULL;
    appletls_t* appletls = (appletls_t*)malloc(sizeof(appletls_t));
    if (appletls == NULL) return NULL;
    appletls->session = NULL;
    appletls->ctx = (appletls_ctx_t*)ssl_ctx;
    appletls->fd = fd;
    return (hssl_t)appletls;
}

static OSStatus SocketRead(SSLConnectionRef conn, void* data, size_t* len) {
    // printf("SocketRead(%d)\n", (int)*len);
    appletls_t* appletls = (appletls_t*)conn;
    uint8_t* buffer = (uint8_t*)data;
    size_t remain = *len;
    *len = 0;
    while (remain) {
        // printf("read(%d)\n", (int)remain);
        ssize_t nread = read(appletls->fd, buffer, remain);
        // printf("nread=%d errno=%d\n", (int)nread, (int)errno);
        if (nread == 0) return errSSLClosedGraceful;
        if (nread < 0) {
            switch (errno) {
            case ENOENT:    return errSSLClosedGraceful;
            case ECONNRESET:return errSSLClosedAbort;
            case EAGAIN:    return errSSLWouldBlock;
            default:        return errSSLClosedAbort;
            }
        }
        remain -= nread;
        buffer += nread;
        *len += nread;
    }
    return noErr;
}

static OSStatus SocketWrite(SSLConnectionRef conn, const void* data, size_t* len) {
    // printf("SocketWrite(%d)\n", (int)*len);
    appletls_t* appletls = (appletls_t*)conn;
    uint8_t* buffer = (uint8_t*)data;
    size_t remain = *len;
    *len = 0;
    while (remain) {
        // printf("write(%d)\n", (int)remain);
        ssize_t nwrite = write(appletls->fd, buffer, remain);
        // printf("nwrite=%d errno=%d\n", (int)nwrite, (int)errno);
        if (nwrite <= 0) {
            switch (errno) {
            case EAGAIN:    return errSSLWouldBlock;
            default:        return errSSLClosedAbort;
            }
        }
        remain -= nwrite;
        buffer += nwrite;
        *len += nwrite;
    }
    return noErr;
}

static int hssl_init(hssl_t ssl, int endpoint) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    OSStatus ret = noErr;
    if (appletls->session == NULL) {
#if defined(__MAC_10_8)
        appletls->session = SSLCreateContext(NULL, endpoint == HSSL_SERVER ? kSSLServerSide : kSSLClientSide, kSSLStreamType);
#else
        SSLNewContext(endpoint == HSSL_SERVER, &(appletls->session));
#endif
    }
    if (appletls->session == NULL) {
        fprintf(stderr, "SSLCreateContext failed!\n");
        return HSSL_ERROR;
    }

    ret = SSLSetProtocolVersionEnabled(appletls->session, kSSLProtocolAll, true);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetProtocolVersionEnabled failed!\n");
        return HSSL_ERROR;
    }

    bool verify_peer = false;
    if (appletls->ctx->param && appletls->ctx->param->verify_peer) {
        verify_peer = true;
    }
#if defined(__MAC_10_8)
    ret = SSLSetSessionOption(appletls->session, kSSLSessionOptionBreakOnServerAuth, !verify_peer);
#else
    ret = SSLSetEnableCertVerify(appletls->session, verify_peer);
#endif
    if (ret != noErr) {
        fprintf(stderr, "SSLSetEnableCertVerify failed!\n");
        return HSSL_ERROR;
    }

    if (appletls->ctx->cert) {
        CFArrayRef certs = CFArrayCreate(NULL, (const void**)&appletls->ctx->cert, 1, NULL);
        if (!certs) {
            fprintf(stderr, "CFArrayCreate failed!\n");
            return HSSL_ERROR;
        }
        ret = SSLSetCertificate(appletls->session, certs);
        CFRelease(certs);
        if (ret != noErr) {
            fprintf(stderr, "SSLSetCertificate failed!\n");
            return HSSL_ERROR;
        }
    }

    size_t all_ciphers_count = 0, allowed_ciphers_count = 0;
    SSLCipherSuite *all_ciphers = NULL, *allowed_ciphers = NULL;
    ret = SSLGetNumberSupportedCiphers(appletls->session, &all_ciphers_count);
    if (ret != noErr) {
        fprintf(stderr, "SSLGetNumberSupportedCiphers failed!\n");
        goto error;
    }
    all_ciphers = (SSLCipherSuite*)malloc(all_ciphers_count * sizeof(SSLCipherSuite));
    allowed_ciphers = (SSLCipherSuite*)malloc(all_ciphers_count * sizeof(SSLCipherSuite));
    if (all_ciphers == NULL || allowed_ciphers == NULL) {
        fprintf(stderr, "malloc failed!\n");
        goto error;
    }
    ret = SSLGetSupportedCiphers(appletls->session, all_ciphers, &all_ciphers_count);
    if (ret != noErr) {
        fprintf(stderr, "SSLGetSupportedCiphers failed!\n");
        goto error;
    }
    for (size_t i = 0; i < all_ciphers_count; ++i) {
        /* Disclaimer: excerpted from curl */
        switch(all_ciphers[i]) {
        /* Disable NULL ciphersuites: */
        case SSL_NULL_WITH_NULL_NULL:
        case SSL_RSA_WITH_NULL_MD5:
        case SSL_RSA_WITH_NULL_SHA:
        case 0x003B: /* TLS_RSA_WITH_NULL_SHA256 */
        case SSL_FORTEZZA_DMS_WITH_NULL_SHA:
        case 0xC001: /* TLS_ECDH_ECDSA_WITH_NULL_SHA */
        case 0xC006: /* TLS_ECDHE_ECDSA_WITH_NULL_SHA */
        case 0xC00B: /* TLS_ECDH_RSA_WITH_NULL_SHA */
        case 0xC010: /* TLS_ECDHE_RSA_WITH_NULL_SHA */
        case 0x002C: /* TLS_PSK_WITH_NULL_SHA */
        case 0x002D: /* TLS_DHE_PSK_WITH_NULL_SHA */
        case 0x002E: /* TLS_RSA_PSK_WITH_NULL_SHA */
        case 0x00B0: /* TLS_PSK_WITH_NULL_SHA256 */
        case 0x00B1: /* TLS_PSK_WITH_NULL_SHA384 */
        case 0x00B4: /* TLS_DHE_PSK_WITH_NULL_SHA256 */
        case 0x00B5: /* TLS_DHE_PSK_WITH_NULL_SHA384 */
        case 0x00B8: /* TLS_RSA_PSK_WITH_NULL_SHA256 */
        case 0x00B9: /* TLS_RSA_PSK_WITH_NULL_SHA384 */
        /* Disable anonymous ciphersuites: */
        case SSL_DH_anon_EXPORT_WITH_RC4_40_MD5:
        case SSL_DH_anon_WITH_RC4_128_MD5:
        case SSL_DH_anon_EXPORT_WITH_DES40_CBC_SHA:
        case SSL_DH_anon_WITH_DES_CBC_SHA:
        case SSL_DH_anon_WITH_3DES_EDE_CBC_SHA:
        case TLS_DH_anon_WITH_AES_128_CBC_SHA:
        case TLS_DH_anon_WITH_AES_256_CBC_SHA:
        case 0xC015: /* TLS_ECDH_anon_WITH_NULL_SHA */
        case 0xC016: /* TLS_ECDH_anon_WITH_RC4_128_SHA */
        case 0xC017: /* TLS_ECDH_anon_WITH_3DES_EDE_CBC_SHA */
        case 0xC018: /* TLS_ECDH_anon_WITH_AES_128_CBC_SHA */
        case 0xC019: /* TLS_ECDH_anon_WITH_AES_256_CBC_SHA */
        case 0x006C: /* TLS_DH_anon_WITH_AES_128_CBC_SHA256 */
        case 0x006D: /* TLS_DH_anon_WITH_AES_256_CBC_SHA256 */
        case 0x00A6: /* TLS_DH_anon_WITH_AES_128_GCM_SHA256 */
        case 0x00A7: /* TLS_DH_anon_WITH_AES_256_GCM_SHA384 */
        /* Disable weak key ciphersuites: */
        case SSL_RSA_EXPORT_WITH_RC4_40_MD5:
        case SSL_RSA_EXPORT_WITH_RC2_CBC_40_MD5:
        case SSL_RSA_EXPORT_WITH_DES40_CBC_SHA:
        case SSL_DH_DSS_EXPORT_WITH_DES40_CBC_SHA:
        case SSL_DH_RSA_EXPORT_WITH_DES40_CBC_SHA:
        case SSL_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA:
        case SSL_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA:
        case SSL_RSA_WITH_DES_CBC_SHA:
        case SSL_DH_DSS_WITH_DES_CBC_SHA:
        case SSL_DH_RSA_WITH_DES_CBC_SHA:
        case SSL_DHE_DSS_WITH_DES_CBC_SHA:
        case SSL_DHE_RSA_WITH_DES_CBC_SHA:
        /* Disable IDEA: */
        case SSL_RSA_WITH_IDEA_CBC_SHA:
        case SSL_RSA_WITH_IDEA_CBC_MD5:
        /* Disable RC4: */
        case SSL_RSA_WITH_RC4_128_MD5:
        case SSL_RSA_WITH_RC4_128_SHA:
        case 0xC002: /* TLS_ECDH_ECDSA_WITH_RC4_128_SHA */
        case 0xC007: /* TLS_ECDHE_ECDSA_WITH_RC4_128_SHA*/
        case 0xC00C: /* TLS_ECDH_RSA_WITH_RC4_128_SHA */
        case 0xC011: /* TLS_ECDHE_RSA_WITH_RC4_128_SHA */
        case 0x008A: /* TLS_PSK_WITH_RC4_128_SHA */
        case 0x008E: /* TLS_DHE_PSK_WITH_RC4_128_SHA */
        case 0x0092: /* TLS_RSA_PSK_WITH_RC4_128_SHA */
            break;
        default: /* enable everything else */
            allowed_ciphers[allowed_ciphers_count++] = all_ciphers[i];
            break;
        }
    }
    ret = SSLSetEnabledCiphers(appletls->session, allowed_ciphers, allowed_ciphers_count);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetEnabledCiphers failed!\n");
        goto error;
    }
    if (all_ciphers) {
        free(all_ciphers);
        all_ciphers = NULL;
    }
    if (allowed_ciphers) {
        free(allowed_ciphers);
        allowed_ciphers = NULL;
    }

    ret = SSLSetIOFuncs(appletls->session, SocketRead, SocketWrite);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetIOFuncs failed!\n");
        return HSSL_ERROR;
    }
    ret = SSLSetConnection(appletls->session, appletls);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetConnection failed!\n");
        return HSSL_ERROR;
    }

    /*
    char session_id[64] = {0};
    int session_id_len = snprintf(session_id, sizeof(session_id), "libhv:appletls:%p", appletls->session);
    ret = SSLSetPeerID(appletls->session, session_id, session_id_len);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetPeerID failed!\n");
        return HSSL_ERROR;
    }
    */

    return HSSL_OK;
error:
    if (all_ciphers) {
        free(all_ciphers);
    }
    if (allowed_ciphers) {
        free(allowed_ciphers);
    }
    return HSSL_ERROR;
}

void hssl_free(hssl_t ssl) {
    if (ssl == NULL) return;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session) {
#if defined(__MAC_10_8)
        CFRelease(appletls->session);
#else
        SSLDisposeContext(appletls->session);
#endif
        appletls->session = NULL;
    }
    free(appletls);
}

static int hssl_handshake(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    OSStatus ret = SSLHandshake(appletls->session);
    // printf("SSLHandshake retval=%d\n", (int)ret);
    switch(ret) {
    case noErr:
        break;
    case errSSLWouldBlock:
        return HSSL_WANT_READ;
    case errSSLPeerAuthCompleted: /* peer cert is valid, or was ignored if verification disabled */
        return hssl_handshake(ssl);
    case errSSLBadConfiguration:
        return HSSL_WANT_READ;
    default:
        return HSSL_ERROR;
    }
    return HSSL_OK;
}

int hssl_accept(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session == NULL) {
        hssl_init(ssl, HSSL_SERVER);
    }
    return hssl_handshake(ssl);
}

int hssl_connect(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session == NULL) {
        hssl_init(ssl, HSSL_CLIENT);
    }
    return hssl_handshake(ssl);
}

int hssl_read(hssl_t ssl, void* buf, int len) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    size_t processed = 0;
    // printf("SSLRead(%d)\n", len);
    OSStatus ret = SSLRead(appletls->session, buf, len, &processed);
    // printf("SSLRead retval=%d processed=%d\n", (int)ret, (int)processed);
    switch (ret) {
    case noErr:
        return processed;
    case errSSLWouldBlock:
        return processed ? processed : HSSL_WOULD_BLOCK;
    case errSSLClosedGraceful:
    case errSSLClosedNoNotify:
        return 0;
    default:
        return HSSL_ERROR;
    }
}

int hssl_write(hssl_t ssl, const void* buf, int len) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    size_t processed = 0;
    // printf("SSLWrite(%d)\n", len);
    OSStatus ret = SSLWrite(appletls->session, buf, len, &processed);
    // printf("SSLWrite retval=%d processed=%d\n", (int)ret, (int)processed);
    switch (ret) {
    case noErr:
        return processed;
    case errSSLWouldBlock:
        return processed ? processed : HSSL_WOULD_BLOCK;
    case errSSLClosedGraceful:
    case errSSLClosedNoNotify:
        return 0;
    default:
        return HSSL_ERROR;
    }
}

int hssl_close(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    SSLClose(appletls->session);
    return 0;
}

int hssl_set_sni_hostname(hssl_t ssl, const char* hostname) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session == NULL) {
        hssl_init(ssl, HSSL_CLIENT);
    }
    SSLSetPeerDomainName(appletls->session, hostname, strlen(hostname));
    return 0;
}

#endif // WITH_APPLETLS
