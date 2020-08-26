#include "iowatcher.h"
#ifndef EVENT_IOCP
#include "hevent.h"
#include "hsocket.h"
#include "hlog.h"

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

static void __keepalive_timeout_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        hlogw("keepalive timeout [%s] <=> [%s]",
                SOCKADDR_STR(io->localaddr, localaddrstr),
                SOCKADDR_STR(io->peeraddr, peeraddrstr));
        io->error = ETIMEDOUT;
        hio_close(io);
    }
}

static void __heartbeat_timer_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io && io->heartbeat_fn) {
        io->heartbeat_fn(io);
    }
}

static void __accept_cb(hio_t* io) {
    /*
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printd("accept connfd=%d [%s] <= [%s]\n", io->fd,
            SOCKADDR_STR(io->localaddr, localaddrstr),
            SOCKADDR_STR(io->peeraddr, peeraddrstr));
    */

    if (io->accept_cb) {
        // printd("accept_cb------\n");
        io->accept_cb(io);
        // printd("accept_cb======\n");
    }

    if (io->keepalive_timeout > 0) {
        io->keepalive_timer = htimer_add(io->loop, __keepalive_timeout_cb, io->keepalive_timeout, 1);
        io->keepalive_timer->privdata = io;
    }

    if (io->heartbeat_interval > 0) {
        io->heartbeat_timer = htimer_add(io->loop, __heartbeat_timer_cb, io->heartbeat_interval, INFINITE);
        io->heartbeat_timer->privdata = io;
    }
}

static void __connect_cb(hio_t* io) {
    /*
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printd("connect connfd=%d [%s] => [%s]\n", io->fd,
            SOCKADDR_STR(io->localaddr, localaddrstr),
            SOCKADDR_STR(io->peeraddr, peeraddrstr));
    */
    if (io->connect_timer) {
        htimer_del(io->connect_timer);
        io->connect_timer = NULL;
        io->connect_timeout = 0;
    }

    if (io->connect_cb) {
        // printd("connect_cb------\n");
        io->connect_cb(io);
        // printd("connect_cb======\n");
    }

    if (io->keepalive_timeout > 0) {
        io->keepalive_timer = htimer_add(io->loop, __keepalive_timeout_cb, io->keepalive_timeout, 1);
        io->keepalive_timer->privdata = io;
    }

    if (io->heartbeat_interval > 0) {
        io->heartbeat_timer = htimer_add(io->loop, __heartbeat_timer_cb, io->heartbeat_interval, INFINITE);
        io->heartbeat_timer->privdata = io;
    }
}

static void __read_cb(hio_t* io, void* buf, int readbytes) {
    // printd("> %.*s\n", readbytes, buf);
    if (io->keepalive_timer) {
        htimer_reset(io->keepalive_timer);
    }

    if (io->read_cb) {
        // printd("read_cb------\n");
        io->read_cb(io, buf, readbytes);
        // printd("read_cb======\n");
    }
}

static void __write_cb(hio_t* io, const void* buf, int writebytes) {
    // printd("< %.*s\n", writebytes, buf);
    if (io->keepalive_timer) {
        htimer_reset(io->keepalive_timer);
    }

    if (io->write_cb) {
        // printd("write_cb------\n");
        io->write_cb(io, buf, writebytes);
        // printd("write_cb======\n");
    }
}

static void __close_cb(hio_t* io) {
    // printd("close fd=%d\n", io->fd);
    if (io->connect_timer) {
        htimer_del(io->connect_timer);
        io->connect_timer = NULL;
        io->connect_timeout = 0;
    }

    if (io->close_timer) {
        htimer_del(io->close_timer);
        io->close_timer = NULL;
        io->close_timeout = 0;
    }

    if (io->keepalive_timer) {
        htimer_del(io->keepalive_timer);
        io->keepalive_timer = NULL;
        io->keepalive_timeout = 0;
    }

    if (io->heartbeat_timer) {
        htimer_del(io->heartbeat_timer);
        io->heartbeat_timer = NULL;
        io->heartbeat_interval = 0;
        io->heartbeat_fn = NULL;
    }

    if (io->close_cb) {
        // printd("close_cb------\n");
        io->close_cb(io);
        // printd("close_cb======\n");
    }
}

#ifdef WITH_OPENSSL
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "ssl_ctx.h"

static void ssl_do_handshark(hio_t* io) {
    SSL* ssl = (SSL*)io->ssl;
    printd("ssl handshark...\n");
    int ret = SSL_do_handshake(ssl);
    if (ret == 1) {
        // handshark finish
        iowatcher_del_event(io->loop, io->fd, HV_READ);
        io->events &= ~HV_READ;
        io->cb = NULL;
        printd("ssl handshark finished.\n");
        if (io->accept_cb) {
            __accept_cb(io);
        }
        else if (io->connect_cb) {
            __connect_cb(io);
        }
    }
    else {
        int errcode = SSL_get_error(ssl, ret);
        if (errcode == SSL_ERROR_WANT_READ) {
            if ((io->events & HV_READ) == 0) {
                hio_add(io, ssl_do_handshark, HV_READ);
            }
        }
        else {
            hloge("ssl handshake failed: %d", errcode);
            hio_close(io);
        }
    }
}
#endif

static void nio_accept(hio_t* io) {
    //printd("nio_accept listenfd=%d\n", io->fd);
    socklen_t addrlen;
accept:
    addrlen = sizeof(sockaddr_u);
    int connfd = accept(io->fd, io->peeraddr, &addrlen);
    hio_t* connio = NULL;
    if (connfd < 0) {
        if (socket_errno() == EAGAIN) {
            //goto accept_done;
            return;
        }
        else {
            io->error = socket_errno();
            perror("accept");
            goto accept_error;
        }
    }
    addrlen = sizeof(sockaddr_u);
    getsockname(connfd, io->localaddr, &addrlen);
    connio = hio_get(io->loop, connfd);
    // NOTE: inherit from listenio
    connio->accept_cb = io->accept_cb;
    connio->userdata = io->userdata;

#ifdef WITH_OPENSSL
    if (io->io_type == HIO_TYPE_SSL) {
        SSL_CTX* ssl_ctx = (SSL_CTX*)ssl_ctx_instance();
        if (ssl_ctx == NULL) {
            goto accept_error;
        }
        SSL* ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, connfd);
        connio->ssl = ssl;
        hio_enable_ssl(connio);
        //int ret = SSL_accept(ssl);
        SSL_set_accept_state(ssl);
        ssl_do_handshark(connio);
    }
#endif

    if (connio->io_type != HIO_TYPE_SSL) {
        // NOTE: SSL call accept_cb after handshark finished
        __accept_cb(connio);
    }
    goto accept;

accept_error:
    hio_close(io);
}

static void nio_connect(hio_t* io) {
    //printd("nio_connect connfd=%d\n", io->fd);
    socklen_t addrlen = sizeof(sockaddr_u);
    int ret = getpeername(io->fd, io->peeraddr, &addrlen);
    if (ret < 0) {
        io->error = socket_errno();
        printd("connect failed: %s: %d\n", strerror(socket_errno()), socket_errno());
        goto connect_failed;
    }
    else {
        addrlen = sizeof(sockaddr_u);
        getsockname(io->fd, io->localaddr, &addrlen);

#ifdef WITH_OPENSSL
        if (io->io_type == HIO_TYPE_SSL) {
            SSL_CTX* ssl_ctx = (SSL_CTX*)ssl_ctx_instance();
            if (ssl_ctx == NULL) {
                goto connect_failed;
            }
            SSL* ssl = SSL_new(ssl_ctx);
            SSL_set_fd(ssl, io->fd);
            io->ssl = ssl;
            //int ret = SSL_connect(ssl);
            SSL_set_connect_state(ssl);
            ssl_do_handshark(io);
        }
#endif

        if (io->io_type != HIO_TYPE_SSL) {
            // NOTE: SSL call connect_cb after handshark finished
            __connect_cb(io);
        }
        return;
    }

connect_failed:
    hio_close(io);
}

static int __nio_read(hio_t* io, void* buf, int len) {
    int nread = 0;
    switch (io->io_type) {
#ifdef WITH_OPENSSL
    case HIO_TYPE_SSL:
        nread = SSL_read((SSL*)io->ssl, buf, len);
        break;
#endif
    case HIO_TYPE_TCP:
#ifdef OS_UNIX
        nread = read(io->fd, buf, len);
#else
        nread = recv(io->fd, buf, len, 0);
#endif
        break;
    case HIO_TYPE_UDP:
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
    return nread;
}

static int __nio_write(hio_t* io, const void* buf, int len) {
    int nwrite = 0;
    switch (io->io_type) {
#ifdef WITH_OPENSSL
    case HIO_TYPE_SSL:
        nwrite = SSL_write((SSL*)io->ssl, buf, len);
        break;
#endif
    case HIO_TYPE_TCP:
#ifdef OS_UNIX
        nwrite = write(io->fd, buf, len);
#else
        nwrite = send(io->fd, buf, len, 0);
#endif
        break;
    case HIO_TYPE_UDP:
    case HIO_TYPE_IP:
        nwrite = sendto(io->fd, buf, len, 0, io->peeraddr, sizeof(sockaddr_u));
        break;
    default:
        nwrite = write(io->fd, buf, len);
        break;
    }
    return nwrite;
}

static void nio_read(hio_t* io) {
    //printd("nio_read fd=%d\n", io->fd);
    if (io->readbuf.base == NULL || io->readbuf.len == 0) {
        hio_set_readbuf(io, io->loop->readbuf.base, io->loop->readbuf.len);
    }
    void* buf = io->readbuf.base;
    int   len = io->readbuf.len;
    int   nread = 0;
read:
    nread = __nio_read(io, buf, len);
    //printd("read retval=%d\n", nread);
    if (nread < 0) {
        if (socket_errno() == EAGAIN) {
            //goto read_done;
            return;
        }
        else {
            io->error = socket_errno();
            // perror("read");
            goto read_error;
        }
    }
    if (nread == 0) {
        goto disconnect;
    }
    __read_cb(io, buf, nread);
    if (nread == len) {
        goto read;
    }
    return;
read_error:
disconnect:
    hio_close(io);
}

static void nio_write(hio_t* io) {
    //printd("nio_write fd=%d\n", io->fd);
    int nwrite = 0;
write:
    if (write_queue_empty(&io->write_queue)) {
        if (io->close) {
            io->close = 0;
            hio_close(io);
        }
        return;
    }
    offset_buf_t* pbuf = write_queue_front(&io->write_queue);
    char* buf = pbuf->base + pbuf->offset;
    int len = pbuf->len - pbuf->offset;
    nwrite = __nio_write(io, buf, len);
    //printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        if (socket_errno() == EAGAIN) {
            //goto write_done;
            return;
        }
        else {
            io->error = socket_errno();
            // perror("write");
            goto write_error;
        }
    }
    if (nwrite == 0) {
        goto disconnect;
    }
    __write_cb(io, buf, nwrite);
    pbuf->offset += nwrite;
    if (nwrite == len) {
        HV_FREE(pbuf->base);
        write_queue_pop_front(&io->write_queue);
        // write next
        goto write;
    }
    return;
write_error:
disconnect:
    hio_close(io);
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
        if (write_queue_empty(&io->write_queue)) {
            iowatcher_del_event(io->loop, io->fd, HV_WRITE);
            io->events &= ~HV_WRITE;
        }
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
    hio_add(io, hio_handle_events, HV_READ);
    return 0;
}

int hio_connect(hio_t* io) {
    int ret = connect(io->fd, io->peeraddr, SOCKADDR_LEN(io->peeraddr));
#ifdef OS_WIN
    if (ret < 0 && socket_errno() != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && socket_errno() != EINPROGRESS) {
#endif
        perror("connect");
        hio_close(io);
        return ret;
    }
    if (ret == 0) {
        // connect ok
        __connect_cb(io);
        return 0;
    }
    int timeout = io->connect_timeout ? io->connect_timeout : HIO_DEFAULT_CONNECT_TIMEOUT;
    io->connect_timer = htimer_add(io->loop, __connect_timeout_cb, timeout, 1);
    io->connect_timer->privdata = io;
    return hio_add(io, hio_handle_events, HV_WRITE);
}

int hio_read (hio_t* io) {
    return hio_add(io, hio_handle_events, HV_READ);
}

int hio_write (hio_t* io, const void* buf, size_t len) {
    int nwrite = 0;
    if (write_queue_empty(&io->write_queue)) {
try_write:
        nwrite = __nio_write(io, buf, len);
        //printd("write retval=%d\n", nwrite);
        if (nwrite < 0) {
            if (socket_errno() == EAGAIN) {
                nwrite = 0;
                hlogw("try_write failed, enqueue!");
                goto enqueue;
            }
            else {
                // perror("write");
                io->error = socket_errno();
                goto write_error;
            }
        }
        if (nwrite == 0) {
            goto disconnect;
        }
        __write_cb(io, buf, nwrite);
        if (nwrite == len) {
            //goto write_done;
            return nwrite;
        }
enqueue:
        hio_add(io, hio_handle_events, HV_WRITE);
    }
    if (nwrite < len) {
        offset_buf_t rest;
        rest.len = len;
        rest.offset = nwrite;
        // NOTE: free in nio_write
        HV_ALLOC(rest.base, rest.len);
        memcpy(rest.base, (char*)buf, rest.len);
        if (io->write_queue.maxsize == 0) {
            write_queue_init(&io->write_queue, 4);
        }
        write_queue_push_back(&io->write_queue, &rest);
    }
    return nwrite;
write_error:
disconnect:
    hio_close(io);
    return nwrite;
}

int hio_close (hio_t* io) {
    if (io->closed) return 0;
    if (!write_queue_empty(&io->write_queue) && io->error == 0 && io->close == 0) {
        io->close = 1;
        hlogw("write_queue not empty, close later.");
        int timeout_ms = io->close_timeout ? io->close_timeout : HIO_DEFAULT_CLOSE_TIMEOUT;
        io->close_timer = htimer_add(io->loop, __close_timeout_cb, timeout_ms, 1);
        io->close_timer->privdata = io;
        return 0;
    }

    io->closed = 1;
    hio_del(io, HV_RDWR);
    if (io->io_type & HIO_TYPE_SOCKET) {
#ifdef OS_UNIX
        close(io->fd);
#else
        closesocket(io->fd);
#endif
    }
#ifdef WITH_OPENSSL
    if (io->ssl) {
        SSL_free((SSL*)io->ssl);
    }
#endif
    __close_cb(io);
    return 0;
}
#endif
