#include "iowatcher.h"
#ifndef EVENT_IOCP
#include "hevent.h"
#include "hsocket.h"
#include "hlog.h"

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
            io->accept_cb(io);
        }
        else if (io->connect_cb) {
            io->connect_cb(io);
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
        connio->accept_cb = io->accept_cb;
        hio_enable_ssl(connio);
        //int ret = SSL_accept(ssl);
        SSL_set_accept_state(ssl);
        ssl_do_handshark(connio);
    }
#endif

    if (io->io_type != HIO_TYPE_SSL) {
        // NOTE: SSL call accept_cb after handshark finished
        if (io->accept_cb) {
            /*
            char localaddrstr[SOCKADDR_STRLEN] = {0};
            char peeraddrstr[SOCKADDR_STRLEN] = {0};
            printd("accept listenfd=%d connfd=%d [%s] <= [%s]\n", io->fd, connfd,
                    SOCKADDR_STR(io->localaddr, localaddrstr),
                    SOCKADDR_STR(io->peeraddr, peeraddrstr));
            */
            //printd("accept_cb------\n");
            io->accept_cb(connio);
            //printd("accept_cb======\n");
        }
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
        /*
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printd("connect connfd=%d [%s] => [%s]\n", io->fd,
                SOCKADDR_STR(io->localaddr, localaddrstr),
                SOCKADDR_STR(io->peeraddr, peeraddrstr));
        */
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
            if (io->connect_cb) {
                //printd("connect_cb------\n");
                io->connect_cb(io);
                //printd("connect_cb======\n");
            }
        }
        return;
    }

connect_failed:
    hio_close(io);
}

static void nio_read(hio_t* io) {
    //printd("nio_read fd=%d\n", io->fd);
    int nread;
    if (io->readbuf.base == NULL || io->readbuf.len == 0) {
        hio_set_readbuf(io, io->loop->readbuf.base, io->loop->readbuf.len);
    }
    void* buf = io->readbuf.base;
    int   len = io->readbuf.len;
read:
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
    //printd("read retval=%d\n", nread);
    if (nread < 0) {
        if (socket_errno() == EAGAIN) {
            //goto read_done;
            return;
        }
        else {
            io->error = socket_errno();
            /* perror("read"); */
            goto read_error;
        }
    }
    if (nread == 0) {
        goto disconnect;
    }
    //printd("> %.*s\n", nread, buf);
    if (io->read_cb) {
        //printd("read_cb------\n");
        io->read_cb(io, buf, nread);
        //printd("read_cb======\n");
    }
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
        return;
    }
    offset_buf_t* pbuf = write_queue_front(&io->write_queue);
    char* buf = pbuf->base + pbuf->offset;
    int len = pbuf->len - pbuf->offset;
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
    //printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        if (socket_errno() == EAGAIN) {
            //goto write_done;
            return;
        }
        else {
            io->error = socket_errno();
            /* perror("write"); */
            goto write_error;
        }
    }
    if (nwrite == 0) {
        goto disconnect;
    }
    if (io->write_cb) {
        //printd("write_cb------\n");
        io->write_cb(io, buf, nwrite);
        //printd("write_cb======\n");
    }
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
            if (io->timer) {
                htimer_del(io->timer);
                io->timer = NULL;
            }

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

#define CONNECT_TIMEOUT     5000 // ms
static void connect_timeout_cb(htimer_t* timer) {
    hio_close((hio_t*)timer->userdata);
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
        if (io->connect_cb) {
            io->connect_cb(io);
        }
        return 0;
    }
    htimer_t* timer = htimer_add(io->loop, connect_timeout_cb, CONNECT_TIMEOUT, 1);
    timer->userdata = io;
    io->timer = timer;
    return hio_add(io, hio_handle_events, HV_WRITE);
}

int hio_read (hio_t* io) {
    return hio_add(io, hio_handle_events, HV_READ);
}

int hio_write (hio_t* io, const void* buf, size_t len) {
    int nwrite = 0;
    if (write_queue_empty(&io->write_queue)) {
try_write:
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
            nwrite = sendto(io->fd, buf, len, 0, io->peeraddr, SOCKADDR_LEN(io->peeraddr));
            break;
        default:
            nwrite = write(io->fd, buf, len);
            break;
        }
        //printd("write retval=%d\n", nwrite);
        if (nwrite < 0) {
            if (socket_errno() == EAGAIN) {
                nwrite = 0;
                hlogw("try_write failed, enqueue!");
                goto enqueue;
            }
            else {
                /* perror("write"); */
                io->error = socket_errno();
                goto write_error;
            }
        }
        if (nwrite == 0) {
            goto disconnect;
        }
        if (io->write_cb) {
            //printd("try_write_cb------\n");
            io->write_cb(io, buf, nwrite);
            //printd("try_write_cb======\n");
        }
        if (nwrite == len) {
            //goto write_done;
            return nwrite;
        }
        hio_add(io, hio_handle_events, HV_WRITE);
    }
enqueue:
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
    printd("close fd=%d\n", io->fd);
    if (io->closed) return 0;
    io->closed = 1;
    hio_del(io, HV_RDWR);
#ifdef OS_UNIX
    close(io->fd);
#else
    closesocket(io->fd);
#endif
#ifdef WITH_OPENSSL
    if (io->ssl) {
        SSL_free((SSL*)io->ssl);
    }
#endif
    if (io->close_cb) {
        //printd("close_cb------\n");
        io->close_cb(io);
        //printd("close_cb======\n");
    }
    return 0;
}
#endif
