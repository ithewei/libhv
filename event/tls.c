#include "tls.h"

#include "hevent.h"
#include "herr.h"
#include "hlog.h"
#include "hsocket.h"
#include "proxy.h"

static int tls_prepare(hio_t* io, hssl_ctx_t ssl_ctx) {
    if (io->ssl == NULL) {
        if (ssl_ctx == NULL) {
            io->error = ERR_NEW_SSL_CTX;
            hio_close(io);
            return -1;
        }
        io->ssl = hssl_new(ssl_ctx, io->fd);
        if (io->ssl == NULL) {
            io->error = ERR_NEW_SSL;
            hio_close(io);
            return -1;
        }
    }
    hio_enable_ssl(io);
    return 0;
}

static hssl_ctx_t tls_client_ctx(hio_t* io) {
    if (io->ssl_ctx) return io->ssl_ctx;
    if (g_ssl_ctx) return g_ssl_ctx;
    io->ssl_ctx = hssl_ctx_new(NULL);
    if (io->ssl_ctx) io->alloced_ssl_ctx = 1;
    return io->ssl_ctx;
}

int tls_server_handshake_start(hio_t* listenio, hio_t* connio) {
    hssl_ctx_t ssl_ctx = NULL;
    if (connio->ssl == NULL) {
        ssl_ctx = listenio->ssl_ctx;
        if (ssl_ctx == NULL) {
            ssl_ctx = g_ssl_ctx;
        }
        if (ssl_ctx == NULL) {
            listenio->ssl_ctx = ssl_ctx = hssl_ctx_new(NULL);
            if (ssl_ctx) listenio->alloced_ssl_ctx = 1;
        }
    }
    if (tls_prepare(connio, ssl_ctx) != 0) return -1;
    connio->phase = HIO_PHASE_TLS_SERVER_HANDSHAKING;
    tls_handshake_step(connio);
    return connio->closed ? -1 : 0;
}

int tls_client_handshake_start(hio_t* io) {
    hssl_ctx_t ssl_ctx = io->ssl == NULL ? tls_client_ctx(io) : NULL;
    if (tls_prepare(io, ssl_ctx) != 0) return -1;

    const char* sni = NULL;
    if (io->proxy && io->proxy->setting.target_host[0] && !is_ipaddr(io->proxy->setting.target_host)) {
        sni = io->proxy->setting.target_host;
    } else if (io->hostname && !is_ipaddr(io->hostname)) {
        sni = io->hostname;
    }
    if (sni) {
        hssl_set_sni_hostname(io->ssl, sni);
    }

    io->phase = HIO_PHASE_TLS_CLIENT_HANDSHAKING;
    tls_handshake_step(io);
    return io->closed ? -1 : 0;
}

void tls_handshake_step(hio_t* io) {
    bool server = io->phase == HIO_PHASE_TLS_SERVER_HANDSHAKING;
    printd("tls %s handshake...\n", server ? "server" : "client");
    int ret = server ? hssl_accept(io->ssl) : hssl_connect(io->ssl);
    if (ret == HSSL_OK) {
        hio_del(io, HV_RDWR);
        io->phase = HIO_PHASE_TLS_ESTABLISHED;
        printd("tls handshake finished.\n");
        return;
    }
    if (ret == HSSL_WANT_READ) {
        if (io->events & HV_WRITE) {
            hio_del(io, HV_WRITE);
        }
        if ((io->events & HV_READ) == 0) {
            hio_add(io, NULL, HV_READ);
        }
        return;
    }
    if (ret == HSSL_WANT_WRITE) {
        if (io->events & HV_READ) {
            hio_del(io, HV_READ);
        }
        if ((io->events & HV_WRITE) == 0) {
            hio_add(io, NULL, HV_WRITE);
        }
        return;
    }

    hloge("tls %s handshake failed: %d", server ? "server" : "client", ret);
    io->error = ERR_SSL_HANDSHAKE;
    hio_close(io);
}
