#include "iowatcher.h"
#ifndef EVENT_IOCP
#include "hevent.h"
#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"
#include "herr.h"
#include "hthread.h"
#include "socks5.h"
#include "proxy.h"

static void __connect_timeout_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        hlogw("connect timeout [%s] <=> [%s]",
                SOCKADDR_STR(io->localaddr, localaddrstr),
                SOCKADDR_STR(io->peeraddr, peeraddrstr));
        io->error = ETIMEDOUT;
        hio_close(io);
    }
}

static void __close_timeout_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        hlogw("close timeout [%s] <=> [%s]",
                SOCKADDR_STR(io->localaddr, localaddrstr),
                SOCKADDR_STR(io->peeraddr, peeraddrstr));
        io->error = ETIMEDOUT;
        hio_close(io);
    }
}

static void __accept_cb(hio_t* io) {
    hio_accept_cb(io);
}

static void __connect_cb(hio_t* io) {
    hio_del_connect_timer(io);
    hio_connect_cb(io);
}

static void __read_cb(hio_t* io, void* buf, int readbytes) {
    // printd("> %.*s\n", readbytes, buf);
    io->last_read_hrtime = io->loop->cur_hrtime;
    hio_handle_read(io, buf, readbytes);
}

static void __write_cb(hio_t* io, const void* buf, int writebytes) {
    // printd("< %.*s\n", writebytes, buf);
    io->last_write_hrtime = io->loop->cur_hrtime;
    hio_write_cb(io, buf, writebytes);
}

static void __close_cb(hio_t* io) {
    // printd("close fd=%d\n", io->fd);
    hio_del_connect_timer(io);
    hio_del_close_timer(io);
    hio_del_read_timer(io);
    hio_del_write_timer(io);
    hio_del_keepalive_timer(io);
    hio_del_heartbeat_timer(io);
    hio_close_cb(io);
}

static void ssl_server_handshake(hio_t* io) {
    printd("ssl server handshake...\n");
    int ret = hssl_accept(io->ssl);
    if (ret == 0) {
        // handshake finish
        hio_del(io, HV_RDWR);
        printd("ssl handshake finished.\n");
        __accept_cb(io);
    }
    else if (ret == HSSL_WANT_READ) {
        if (io->events & HV_WRITE) {
            hio_del(io, HV_WRITE);
        }
        if ((io->events & HV_READ) == 0) {
            hio_add(io, ssl_server_handshake, HV_READ);
        }
    }
    else if (ret == HSSL_WANT_WRITE) {
        if (io->events & HV_READ) {
            hio_del(io, HV_READ);
        }
        if ((io->events & HV_WRITE) == 0) {
            hio_add(io, ssl_server_handshake, HV_WRITE);
        }
    }
    else {
        hloge("ssl server handshake failed: %d", ret);
        io->error = ERR_SSL_HANDSHAKE;
        hio_close(io);
    }
}

static void ssl_client_handshake(hio_t* io) {
    printd("ssl client handshake...\n");
    int ret = hssl_connect(io->ssl);
    if (ret == 0) {
        // handshake finish
        hio_del(io, HV_RDWR);
        printd("ssl handshake finished.\n");
        __connect_cb(io);
    }
    else if (ret == HSSL_WANT_READ) {
        if (io->events & HV_WRITE) {
            hio_del(io, HV_WRITE);
        }
        if ((io->events & HV_READ) == 0) {
            hio_add(io, ssl_client_handshake, HV_READ);
        }
    }
    else if (ret == HSSL_WANT_WRITE) {
        if (io->events & HV_READ) {
            hio_del(io, HV_READ);
        }
        if ((io->events & HV_WRITE) == 0) {
            hio_add(io, ssl_client_handshake, HV_WRITE);
        }
    }
    else {
        hloge("ssl client handshake failed: %d", ret);
        io->error = ERR_SSL_HANDSHAKE;
        hio_close(io);
    }
}

static void nio_accept(hio_t* io) {
    // printd("nio_accept listenfd=%d\n", io->fd);
    int connfd = 0, err = 0, accept_cnt = 0;
    socklen_t addrlen;
    hio_t* connio = NULL;
    while (accept_cnt++ < 3) {
        addrlen = sizeof(sockaddr_u);
        connfd = accept(io->fd, io->peeraddr, &addrlen);
        if (connfd < 0) {
            err = socket_errno();
            if (err == EAGAIN || err == EINTR) {
                return;
            } else {
                perror("accept");
                io->error = err;
                goto accept_error;
            }
        }
        addrlen = sizeof(sockaddr_u);
        getsockname(connfd, io->localaddr, &addrlen);
        connio = hio_get(io->loop, connfd);
        // NOTE: inherit from listenio
        connio->accept_cb = io->accept_cb;
        connio->userdata = io->userdata;
        connio->proxy = proxy_ctx_dup(io->proxy);
        if (io->unpack_setting) {
            hio_set_unpack(connio, io->unpack_setting);
        }

        if (io->io_type == HIO_TYPE_SSL) {
            if (connio->ssl == NULL) {
                // io->ssl_ctx > g_ssl_ctx > hssl_ctx_new
                hssl_ctx_t ssl_ctx = NULL;
                if (io->ssl_ctx) {
                    ssl_ctx = io->ssl_ctx;
                } else if (g_ssl_ctx) {
                    ssl_ctx = g_ssl_ctx;
                } else {
                    io->ssl_ctx = ssl_ctx = hssl_ctx_new(NULL);
                    io->alloced_ssl_ctx = 1;
                }
                if (ssl_ctx == NULL) {
                    io->error = ERR_NEW_SSL_CTX;
                    goto accept_error;
                }
                hssl_t ssl = hssl_new(ssl_ctx, connfd);
                if (ssl == NULL) {
                    io->error = ERR_NEW_SSL;
                    goto accept_error;
                }
                connio->ssl = ssl;
            }
            hio_enable_ssl(connio);
            ssl_server_handshake(connio);
        }
        else {
            // NOTE: SSL call accept_cb after handshake finished
            __accept_cb(connio);
        }
    }
    return;

accept_error:
    hloge("listenfd=%d accept error: %s:%d", io->fd, socket_strerror(io->error), io->error);
    // NOTE: Don't close listen fd automatically anyway.
    // hio_close(io);
}

// After the transport is connected (and, if using SOCKS5, after the proxy
// handshake completed), start the SSL handshake or deliver connect_cb.
static void nio_connect_established(hio_t* io) {
    if (io->io_type == HIO_TYPE_SSL) {
        if (io->ssl == NULL) {
            // io->ssl_ctx > g_ssl_ctx > hssl_ctx_new
            hssl_ctx_t ssl_ctx = NULL;
            if (io->ssl_ctx) {
                ssl_ctx = io->ssl_ctx;
            } else if (g_ssl_ctx) {
                ssl_ctx = g_ssl_ctx;
            } else {
                io->ssl_ctx = ssl_ctx = hssl_ctx_new(NULL);
                io->alloced_ssl_ctx = 1;
            }
            if (ssl_ctx == NULL) {
                io->error = ERR_NEW_SSL_CTX;
                hio_close(io);
                return;
            }
            hssl_t ssl = hssl_new(ssl_ctx, io->fd);
            if (ssl == NULL) {
                io->error = ERR_NEW_SSL;
                hio_close(io);
                return;
            }
            io->ssl = ssl;
        }
        // SNI: through a proxy the TLS peer is the target, so the proxy's
        // target_host is authoritative; otherwise use the explicitly-set
        // io->hostname. SNI must be a hostname, not an IP literal (RFC 6066),
        // so a numeric candidate is skipped and the next one is considered.
        const char* sni = NULL;
        if (io->proxy && io->proxy->setting.target_host[0] && !is_ipaddr(io->proxy->setting.target_host)) {
            sni = io->proxy->setting.target_host;
        } else if (io->hostname && !is_ipaddr(io->hostname)) {
            sni = io->hostname;
        }
        if (sni) {
            hssl_set_sni_hostname(io->ssl, sni);
        }
        ssl_client_handshake(io);
    }
    else {
        // NOTE: SSL call connect_cb after handshake finished
        __connect_cb(io);
    }
}

// SOCKS5 client handshake state machine (RFC 1928 + RFC 1929).
// Driven via hio_add(io, socks5_handshake, HV_READ), exactly like
// ssl_client_handshake: it does raw recv() into an internal accumulator and
// does NOT touch io->read_cb (which the upper-layer Channel owns for delivering
// user data). Bytes are buffered in s5->rbuf until a full step is available, so
// the handshake is robust to TCP fragmentation. Runs before the optional SSL
// handshake.
enum socks5_state_e {
    S5_RECV_METHOD = 0,     // 2 bytes: VER METHOD
    S5_RECV_AUTH,           // 2 bytes: VER STATUS
    S5_RECV_REPLY_HEAD,     // 4 bytes: VER REP RSV ATYP
    S5_RECV_REPLY_ADDR,     // fixed addr+port (ipv4/ipv6)
    S5_RECV_REPLY_DADDR,    // 1 (dlen) already known: domain + port
};

static void socks5_handshake(hio_t* io);

static void proxy_fail(hio_t* io) {
    if (io->error == 0) io->error = ERR_CONNECT;
    hlogw("connfd=%d proxy handshake error", io->fd);
    hio_close(io);
}

// advance to a new state that needs `want` more bytes, resetting the buffer.
static void socks5_expect(hio_t* io, int state, int want) {
    proxy_ctx_t* s5 = io->proxy;
    s5->state = state;
    s5->rlen = 0;
    s5->want = want;
}

// Raw handshake send. The proxy handshake runs immediately after the TCP
// connection to the proxy is established, when the socket send buffer is empty
// and the message is tiny (SOCKS5 <= 513 bytes; HTTP CONNECT < ~1.3KB), far
// smaller than the default send buffer, so a single send() transfers it all.
// We deliberately do NOT use hio_write() here: it would invoke the upper-layer
// write_cb (leaking handshake bytes, including credentials, to the application
// before onConnection), dispatch to hssl_write() with a not-yet-created SSL
// handle for a TLS target, and enqueue on EAGAIN via hio_add() which would
// clobber the handshake read handler (io has a single cb slot). A short write
// cannot happen here in practice; if it somehow does, it is treated as fatal
// (return -1) rather than blocking the event loop.
static int proxy_send(hio_t* io, const void* buf, int len) {
    int flag = 0;
#ifdef MSG_NOSIGNAL
    flag |= MSG_NOSIGNAL;
#endif
    int n = send(io->fd, (const char*)buf, len, flag);
    return n == len ? 0 : -1;
}

// send the SOCKS5 CONNECT request and wait for the 4-byte reply header.
static void socks5_send_connect(hio_t* io) {
    proxy_ctx_t* s5 = io->proxy;
    unsigned char buf[300];
    int n = socks5_build_connect_request(s5, buf);
    if (n < 0) { proxy_fail(io); return; }
    if (proxy_send(io, buf, n) != 0) { proxy_fail(io); return; }
    socks5_expect(io, S5_RECV_REPLY_HEAD, 4);
}

// hand off the established proxy tunnel to the upper layer: stop the handshake
// read handler, then run the SSL handshake / connect_cb. io->read_cb was never
// touched, so the upper-layer Channel read callback stays intact.
static void proxy_established(hio_t* io) {
    hio_del(io, HV_READ);
    nio_connect_established(io);
}

// process one accumulated step; s5->rbuf holds exactly s5->want bytes.
static void socks5_dispatch(hio_t* io) {
    proxy_ctx_t* s5 = io->proxy;
    unsigned char* buf = s5->rbuf;

    switch (s5->state) {
    case S5_RECV_METHOD:
        // VER METHOD
        if (buf[0] != SOCKS5_VERSION) { proxy_fail(io); return; }
        if (buf[1] == SOCKS5_AUTH_NONE) {
            socks5_send_connect(io);
        } else if (buf[1] == SOCKS5_AUTH_USERPASS && s5->setting.username[0]) {
            unsigned char req[640];
            int n = socks5_build_auth_request(s5, req);
            if (proxy_send(io, req, n) != 0) { proxy_fail(io); return; }
            socks5_expect(io, S5_RECV_AUTH, 2);
        } else {
            proxy_fail(io);   // no acceptable method
        }
        return;

    case S5_RECV_AUTH:
        // VER STATUS (0 == success)
        if (buf[1] != 0x00) { proxy_fail(io); return; }
        socks5_send_connect(io);
        return;

    case S5_RECV_REPLY_HEAD: {
        // VER REP RSV ATYP
        if (buf[0] != SOCKS5_VERSION) { proxy_fail(io); return; }
        if (buf[1] != SOCKS5_REP_SUCCESS) { io->error = ERR_CONNECT; proxy_fail(io); return; }
        unsigned char atyp = buf[3];
        if (atyp == SOCKS5_ATYP_IPV4) {
            socks5_expect(io, S5_RECV_REPLY_ADDR, 4 + 2);   // addr + port
        } else if (atyp == SOCKS5_ATYP_IPV6) {
            socks5_expect(io, S5_RECV_REPLY_ADDR, 16 + 2);
        } else if (atyp == SOCKS5_ATYP_DOMAIN) {
            // read 1 length byte + then domain+port; do it in one extra step by
            // first requiring the length byte.
            socks5_expect(io, S5_RECV_REPLY_DADDR, 1);
        } else {
            proxy_fail(io);
        }
        return;
    }

    case S5_RECV_REPLY_ADDR:
        // bound addr+port consumed; tunnel is up
        proxy_established(io);
        return;

    case S5_RECV_REPLY_DADDR:
        // first entry: we have the 1-byte domain length -> need dlen + 2 more.
        // Re-enter with the full length once available.
        if (s5->want == 1) {
            int dlen = buf[0];
            socks5_expect(io, S5_RECV_REPLY_DADDR, dlen + 2);
            return;
        }
        proxy_established(io);
        return;

    default:
        proxy_fail(io);
        return;
    }
}

// hio_add read handler: accumulate into s5->rbuf until s5->want bytes are
// available, then dispatch. Never touches io->read_cb.
static void socks5_handshake(hio_t* io) {
    proxy_ctx_t* s5 = io->proxy;
    while (s5->rlen < s5->want) {
        int need = s5->want - s5->rlen;
        if (s5->want > (int)sizeof(s5->rbuf)) { proxy_fail(io); return; }
        int n = recv(io->fd, (char*)s5->rbuf + s5->rlen, need, 0);
        if (n == 0) { proxy_fail(io); return; }            // peer closed
        if (n < 0) {
            int err = socket_errno();
            if (err == EAGAIN || err == EINTR) return;    // wait for more
            io->error = err;
            proxy_fail(io);
            return;
        }
        s5->rlen += n;
    }
    socks5_dispatch(io);
}

// Kick off the SOCKS5 handshake once the TCP connection to the proxy is up.
static void socks5_handshake_start(hio_t* io) {
    proxy_ctx_t* s5 = io->proxy;
    unsigned char buf[8];
    int n = socks5_build_method_request(s5, buf);
    if (proxy_send(io, buf, n) != 0) { proxy_fail(io); return; }
    socks5_expect(io, S5_RECV_METHOD, 2);
    hio_add(io, socks5_handshake, HV_READ);
}

// HTTP CONNECT handshake (RFC 7231 4.3.6): send a CONNECT request, then read
// response headers until the blank line "\r\n\r\n". A 2xx status establishes
// the tunnel. Like the SOCKS5 handshake this uses a dedicated recv() via
// hio_add (never touches io->read_cb) and is robust to fragmentation.
//
// CONNECT responses carry no body, but a server-first origin protocol (SMTP,
// IMAP, FTP...) may send its greeting immediately after the tunnel opens, so
// those bytes can arrive in the same segment as the response headers. To avoid
// swallowing them, we MSG_PEEK to locate the header terminator, then drain
// EXACTLY the header bytes with a real recv(); anything after "\r\n\r\n" stays
// in the socket for the upper-layer read path.
static void http_connect_handshake(hio_t* io) {
    proxy_ctx_t* p = io->proxy;
    for (;;) {
        int cap = (int)sizeof(p->rbuf) - p->rlen;
        if (cap <= 0) { proxy_fail(io); return; }   // headers too large
        // peek (non-destructive): inspect what is available without consuming.
        int n = recv(io->fd, (char*)p->rbuf + p->rlen, cap, MSG_PEEK);
        if (n == 0) { proxy_fail(io); return; }      // peer closed
        if (n < 0) {
            int err = socket_errno();
            if (err == EAGAIN || err == EINTR) return;   // wait for more
            io->error = err;
            proxy_fail(io);
            return;
        }
        int have = p->rlen + n;
        // search for "\r\n\r\n" in the peeked window (rescan from a safe offset)
        int start = p->rlen >= 3 ? p->rlen - 3 : 0;
        int term = -1;
        for (int i = start + 3; i < have; ++i) {
            if (p->rbuf[i-3]=='\r' && p->rbuf[i-2]=='\n' &&
                p->rbuf[i-1]=='\r' && p->rbuf[i]=='\n') { term = i; break; }
        }
        if (term < 0) {
            // no full header yet: consume the peeked bytes into the accumulator
            // (they are all header bytes) and keep reading.
            int got = recv(io->fd, (char*)p->rbuf + p->rlen, n, 0);
            if (got <= 0) { proxy_fail(io); return; }
            p->rlen += got;
            continue;
        }
        // full header present. Drain exactly up to and including the terminator,
        // leaving any trailing tunnel/greeting bytes in the socket.
        int header_len = term + 1;              // bytes from socket start
        int to_drain = header_len - p->rlen;    // not yet consumed
        if (to_drain > 0) {
            int got = recv(io->fd, (char*)p->rbuf + p->rlen, to_drain, 0);
            if (got != to_drain) { proxy_fail(io); return; }
            p->rlen += got;
        }
        // parse status line: "HTTP/1.x SP CODE SP ..."
        int code = 0;
        char* sp = (char*)memchr(p->rbuf, ' ', p->rlen);
        if (sp) code = atoi(sp + 1);
        if (code >= 200 && code < 300) {
            proxy_established(io);
        } else {
            hlogw("connfd=%d http proxy CONNECT failed: %d", io->fd, code);
            io->error = ERR_CONNECT;
            proxy_fail(io);
        }
        return;
    }
}

// Kick off the HTTP CONNECT handshake once the TCP connection to the proxy is up.
static void http_connect_start(hio_t* io) {
    proxy_ctx_t* p = io->proxy;
    // Max request: "CONNECT " + authority(<=262) + " HTTP/1.1\r\nHost: " +
    // authority + "\r\nProxy-Authorization: Basic " + base64(255:255)=~684 +
    // "\r\n\r\n" ~= 1.3KB. 2048 leaves headroom.
    char buf[2048];
    int n = http_connect_build_request(p, buf, (int)sizeof(buf));
    if (n < 0) { proxy_fail(io); return; }
    if (proxy_send(io, buf, n) != 0) { proxy_fail(io); return; }
    p->rlen = 0;
    hio_add(io, http_connect_handshake, HV_READ);
}

// Dispatch the proxy handshake by protocol.
static void proxy_handshake_start(hio_t* io) {
    switch (io->proxy->setting.protocol) {
    case PROXY_PROTOCOL_SOCKS5:
        socks5_handshake_start(io);
        return;
    case PROXY_PROTOCOL_HTTP_CONNECT:
        http_connect_start(io);
        return;
    default:
        io->error = ERR_INVALID_PARAM;
        hio_close(io);
        return;
    }
}

static void nio_connect(hio_t* io) {
    // printd("nio_connect connfd=%d\n", io->fd);
    socklen_t addrlen = sizeof(sockaddr_u);
    int ret = getpeername(io->fd, io->peeraddr, &addrlen);
    if (ret < 0) {
        io->error = socket_errno();
        goto connect_error;
    }
    else {
        addrlen = sizeof(sockaddr_u);
        getsockname(io->fd, io->localaddr, &addrlen);

        // Proxy: the TCP connection is to the proxy; run the proxy handshake
        // (CONNECT to the real target) before SSL / connect_cb.
        if (io->proxy) {
            proxy_handshake_start(io);
            return;
        }

        nio_connect_established(io);
        return;
    }

connect_error:
    hlogw("connfd=%d connect error: %s:%d", io->fd, socket_strerror(io->error), io->error);
    hio_close(io);
}

static void nio_connect_event_cb(hevent_t* ev) {
    hio_t* io = (hio_t*)ev->userdata;
    uint32_t id = (uintptr_t)ev->privdata;
    if (io->id != id) return;
    nio_connect(io);
}

static int nio_connect_async(hio_t* io) {
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.cb = nio_connect_event_cb;
    ev.userdata = io;
    ev.privdata = (void*)(uintptr_t)io->id;
    hloop_post_event(io->loop, &ev);
    return 0;
}

static int __nio_read(hio_t* io, void* buf, int len) {
    int nread = 0;
    switch (io->io_type) {
    case HIO_TYPE_SSL:
        nread = hssl_read(io->ssl, buf, len);
        break;
    case HIO_TYPE_TCP:
        nread = recv(io->fd, buf, len, 0);
        break;
    case HIO_TYPE_UDP:
    case HIO_TYPE_KCP:
    case HIO_TYPE_IP:
    {
        socklen_t addrlen = sizeof(sockaddr_u);
        nread = recvfrom(io->fd, buf, len, 0, io->peeraddr, &addrlen);
    }
        break;
    default:
        nread = read(io->fd, buf, len);
        break;
    }
    // hlogd("read retval=%d", nread);
    return nread;
}

static int __nio_write(hio_t* io, const void* buf, int len, struct sockaddr* addr) {
    int nwrite = 0;
    switch (io->io_type) {
    case HIO_TYPE_SSL:
        nwrite = hssl_write(io->ssl, buf, len);
        break;
    case HIO_TYPE_TCP:
    {
        int flag = 0;
#ifdef MSG_NOSIGNAL
        flag |= MSG_NOSIGNAL;
#endif
        nwrite = send(io->fd, buf, len, flag);
    }
        break;
    case HIO_TYPE_UDP:
    case HIO_TYPE_KCP:
    case HIO_TYPE_IP:
    {
        if (addr == NULL) addr = io->peeraddr;
        nwrite = sendto(io->fd, buf, len, 0, addr, SOCKADDR_LEN(addr));
        if (((sockaddr_u*)io->localaddr)->sin.sin_port == 0) {
            socklen_t addrlen = sizeof(sockaddr_u);
            getsockname(io->fd, io->localaddr, &addrlen);
        }
    }
        break;
    default:
        nwrite = write(io->fd, buf, len);
        break;
    }
    // hlogd("write retval=%d", nwrite);
    return nwrite;
}

static void nio_read(hio_t* io) {
    // printd("nio_read fd=%d\n", io->fd);
    void* buf;
    int len = 0, nread = 0, err = 0;
read:
    buf = io->readbuf.base + io->readbuf.tail;
    if (io->read_flags & HIO_READ_UNTIL_LENGTH) {
        len = io->read_until_length - (io->readbuf.tail - io->readbuf.head);
    } else {
        len = io->readbuf.len - io->readbuf.tail;
    }
    assert(len > 0);
    nread = __nio_read(io, buf, len);
    // printd("read retval=%d\n", nread);
    if (nread < 0) {
        err = socket_errno();
        if (err == EAGAIN || err == EINTR) {
            // goto read_done;
            return;
        } else if (err == EMSGSIZE) {
            nread = len;
        } else {
            // perror("read");
            io->error = err;
            goto read_error;
        }
    }
    if (nread == 0 && (io->io_type & HIO_TYPE_SOCK_STREAM)) {
        goto disconnect;
    }
    if (nread < len) {
        // NOTE: make string friendly
        ((char*)buf)[nread] = '\0';
    }
    io->readbuf.tail += nread;
    __read_cb(io, buf, nread);
    if (nread == len && !io->closed) {
        // NOTE: ssl may have own cache
        if (io->io_type == HIO_TYPE_SSL) {
            // read continue
            goto read;
        }
    }
    return;
read_error:
disconnect:
    if (io->io_type & HIO_TYPE_SOCK_STREAM) {
        hio_close(io);
    }
}

static void nio_write(hio_t* io) {
    // printd("nio_write fd=%d\n", io->fd);
    int nwrite = 0, err = 0;
    hrecursive_mutex_lock(&io->write_mutex);
write:
    if (write_queue_empty(&io->write_queue)) {
        hrecursive_mutex_unlock(&io->write_mutex);
        if (io->close) {
            io->close = 0;
            hio_close(io);
        }
        return;
    }
    offset_buf_t* pbuf = write_queue_front(&io->write_queue);
    char* base = pbuf->base;
    char* buf = base + pbuf->offset;
    int len = pbuf->len - pbuf->offset;
    struct sockaddr* addr = NULL;
    if (io->io_type & (HIO_TYPE_SOCK_DGRAM | HIO_TYPE_SOCK_RAW)) {
        addr = (struct sockaddr*)base;
    }
    nwrite = __nio_write(io, buf, len, addr);
    // printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        err = socket_errno();
        if (err == EAGAIN || err == EINTR) {
            hrecursive_mutex_unlock(&io->write_mutex);
            return;
        } else {
            // perror("write");
            io->error = err;
            goto write_error;
        }
    }
    if (nwrite == 0 && (io->io_type & HIO_TYPE_SOCK_STREAM)) {
        goto disconnect;
    }
    pbuf->offset += nwrite;
    io->write_bufsize -= nwrite;
    __write_cb(io, buf, nwrite);
    if (nwrite == len) {
        // NOTE: after write_cb, pbuf maybe invalid.
        // HV_FREE(pbuf->base);
        HV_FREE(base);
        write_queue_pop_front(&io->write_queue);
        if (!io->closed) {
            // write continue
            goto write;
        }
    }
    hrecursive_mutex_unlock(&io->write_mutex);
    return;
write_error:
disconnect:
    hrecursive_mutex_unlock(&io->write_mutex);
    if (io->io_type & HIO_TYPE_SOCK_STREAM) {
        hio_close(io);
    }
}

static void hio_handle_events(hio_t* io) {
    if ((io->events & HV_READ) && (io->revents & HV_READ)) {
        if (io->accept) {
            nio_accept(io);
        }
        else {
            nio_read(io);
        }
    }

    if ((io->events & HV_WRITE) && (io->revents & HV_WRITE)) {
        // NOTE: del HV_WRITE, if write_queue empty
        hrecursive_mutex_lock(&io->write_mutex);
        if (write_queue_empty(&io->write_queue)) {
            hio_del(io, HV_WRITE);
        }
        hrecursive_mutex_unlock(&io->write_mutex);
        if (io->connect) {
            // NOTE: connect just do once
            // ONESHOT
            io->connect = 0;

            nio_connect(io);
        }
        else {
            nio_write(io);
        }
    }

    io->revents = 0;
}

int hio_accept(hio_t* io) {
    io->accept = 1;
    return hio_add(io, hio_handle_events, HV_READ);
}

int hio_connect(hio_t* io) {
    int ret = connect(io->fd, io->peeraddr, SOCKADDR_LEN(io->peeraddr));
#ifdef OS_WIN
    if (ret < 0 && socket_errno() != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && socket_errno() != EINPROGRESS) {
#endif
        perror("connect");
        io->error = socket_errno();
        hio_close_async(io);
        return ret;
    }
    if (ret == 0) {
        // connect ok
        nio_connect_async(io);
        return 0;
    }
    int timeout = io->connect_timeout ? io->connect_timeout : HIO_DEFAULT_CONNECT_TIMEOUT;
    io->connect_timer = htimer_add(io->loop, __connect_timeout_cb, timeout, 1);
    io->connect_timer->privdata = io;
    io->connect = 1;
    return hio_add(io, hio_handle_events, HV_WRITE);
}

int hio_read (hio_t* io) {
    if (io->closed) {
        hloge("hio_read called but fd[%d] already closed!", io->fd);
        return -1;
    }
    hio_add(io, hio_handle_events, HV_READ);
    if (io->readbuf.tail > io->readbuf.head &&
        io->unpack_setting == NULL &&
        io->read_flags == 0) {
        hio_read_remain(io);
    }
    return 0;
}

static int hio_write4 (hio_t* io, const void* buf, size_t len, struct sockaddr* addr) {
    if (io->closed) {
        hloge("hio_write called but fd[%d] already closed!", io->fd);
        return -1;
    }
    int nwrite = 0, err = 0;
    hrecursive_mutex_lock(&io->write_mutex);
#if WITH_KCP
    if (io->io_type == HIO_TYPE_KCP) {
        nwrite = hio_write_kcp(io, buf, len, addr);
        // if (nwrite < 0) goto write_error;
        goto write_done;
    }
#endif
    if (write_queue_empty(&io->write_queue)) {
try_write:
        nwrite = __nio_write(io, buf, len, addr);
        // printd("write retval=%d\n", nwrite);
        if (nwrite < 0) {
            err = socket_errno();
            if (err == EAGAIN || err == EINTR) {
                nwrite = 0;
                hlogw("try_write failed, enqueue!");
                goto enqueue;
            } else {
                // perror("write");
                io->error = err;
                goto write_error;
            }
        }
        if (nwrite == len) {
            goto write_done;
        }
        if (nwrite == 0 && (io->io_type & HIO_TYPE_SOCK_STREAM)) {
            goto disconnect;
        }
enqueue:
        hio_add(io, hio_handle_events, HV_WRITE);
    }
    if (nwrite < len) {
        size_t unwritten_len = len - nwrite;
        if (io->write_bufsize + unwritten_len > io->max_write_bufsize) {
            hloge("write bufsize > %u, close it!", io->max_write_bufsize);
            io->error = ERR_OVER_LIMIT;
            goto write_error;
        }
        size_t addrlen = 0;
        if ((io->io_type & (HIO_TYPE_SOCK_DGRAM | HIO_TYPE_SOCK_RAW)) && addr) {
            addrlen = SOCKADDR_LEN(addr);
        }
        offset_buf_t remain;
        remain.offset = addrlen;
        remain.len = addrlen + unwritten_len;
        // NOTE: free in nio_write
        HV_ALLOC(remain.base, remain.len);
        if (addr && addrlen > 0) {
            memcpy(remain.base, addr, addrlen);
        }
        memcpy(remain.base + remain.offset, ((char*)buf) + nwrite, unwritten_len);
        if (io->write_queue.maxsize == 0) {
            write_queue_init(&io->write_queue, 4);
        }
        write_queue_push_back(&io->write_queue, &remain);
        io->write_bufsize += unwritten_len;
        if (io->write_bufsize > WRITE_BUFSIZE_HIGH_WATER) {
            hlogw("write len=%u enqueue %u, bufsize=%u over high water %u",
                (unsigned int)len,
                (unsigned int)unwritten_len,
                (unsigned int)io->write_bufsize,
                (unsigned int)WRITE_BUFSIZE_HIGH_WATER);
        }
    }
write_done:
    hrecursive_mutex_unlock(&io->write_mutex);
    if (nwrite > 0) {
        __write_cb(io, buf, nwrite);
    }
    return nwrite;
write_error:
disconnect:
    hrecursive_mutex_unlock(&io->write_mutex);
    /* NOTE:
     * We usually free resources in hclose_cb,
     * if hio_close_sync, we have to be very careful to avoid using freed resources.
     * But if hio_close_async, we do not have to worry about this.
     */
    if (io->io_type & HIO_TYPE_SOCK_STREAM) {
        hio_close_async(io);
    }
    return nwrite < 0 ? nwrite : -1;
}

int hio_write (hio_t* io, const void* buf, size_t len) {
    return hio_write4(io, buf, len, io->peeraddr);
}

int hio_sendto (hio_t* io, const void* buf, size_t len, struct sockaddr* addr) {
    return hio_write4(io, buf, len, addr ? addr : io->peeraddr);
}

int hio_close (hio_t* io) {
    if (io->closed) return 0;
    if (io->destroy == 0 && hv_gettid() != io->loop->tid) {
        return hio_close_async(io);
    }

    hrecursive_mutex_lock(&io->write_mutex);
    if (io->closed) {
        hrecursive_mutex_unlock(&io->write_mutex);
        return 0;
    }
    if (!write_queue_empty(&io->write_queue) && io->error == 0 && io->close == 0 && io->destroy == 0) {
        io->close = 1;
        hrecursive_mutex_unlock(&io->write_mutex);
        hlogw("write_queue not empty, close later.");
        int timeout_ms = io->close_timeout ? io->close_timeout : HIO_DEFAULT_CLOSE_TIMEOUT;
        io->close_timer = htimer_add(io->loop, __close_timeout_cb, timeout_ms, 1);
        io->close_timer->privdata = io;
        return 0;
    }
    io->closed = 1;
    hrecursive_mutex_unlock(&io->write_mutex);

    hio_done(io);
    __close_cb(io);
    if (io->ssl) {
        hssl_free(io->ssl);
        io->ssl = NULL;
    }
    if (io->ssl_ctx && io->alloced_ssl_ctx) {
        hssl_ctx_free(io->ssl_ctx);
        io->ssl_ctx = NULL;
    }
    SAFE_FREE(io->hostname);
    proxy_ctx_free(io->proxy);
    io->proxy = NULL;
    if (io->io_type & HIO_TYPE_SOCKET) {
        closesocket(io->fd);
    } else if (io->io_type == HIO_TYPE_PIPE) {
        close(io->fd);
    }
    return 0;
}
#endif
