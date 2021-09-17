#include "iowatcher.h"
#ifndef EVENT_IOCP
#include "hevent.h"
#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"
#include "hthread.h"
#include "unpack.h"

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
    if (io->keepalive_timer) {
        htimer_reset(io->keepalive_timer);
    }

    if (io->unpack_setting) {
        hio_unpack(io, buf, readbytes);
        return;
    }

    if (io->read_once) {
        hio_read_stop(io);
    }

    hio_read_cb(io, buf, readbytes);
}

static void __write_cb(hio_t* io, const void* buf, int writebytes) {
    // printd("< %.*s\n", writebytes, buf);
    if (io->keepalive_timer) {
        htimer_reset(io->keepalive_timer);
    }
    hio_write_cb(io, buf, writebytes);
}

static void __close_cb(hio_t* io) {
    // printd("close fd=%d\n", io->fd);
    hio_del_connect_timer(io);
    hio_del_close_timer(io);
    hio_del_keepalive_timer(io);
    hio_del_heartbeat_timer(io);
    hio_close_cb(io);
}

static void ssl_server_handshake(hio_t* io) {
    printd("ssl server handshake...\n");
    int ret = hssl_accept(io->ssl);
    if (ret == 0) {
        // handshake finish
        iowatcher_del_event(io->loop, io->fd, HV_READ);
        io->events &= ~HV_READ;
        io->cb = NULL;
        printd("ssl handshake finished.\n");
        __accept_cb(io);
    }
    else if (ret == HSSL_WANT_READ) {
        if ((io->events & HV_READ) == 0) {
            hio_add(io, ssl_server_handshake, HV_READ);
        }
    }
    else {
        hloge("ssl handshake failed: %d", ret);
        hio_close(io);
    }
}

static void ssl_client_handshake(hio_t* io) {
    printd("ssl client handshake...\n");
    int ret = hssl_connect(io->ssl);
    if (ret == 0) {
        // handshake finish
        iowatcher_del_event(io->loop, io->fd, HV_READ);
        io->events &= ~HV_READ;
        io->cb = NULL;
        printd("ssl handshake finished.\n");
        __connect_cb(io);
    }
    else if (ret == HSSL_WANT_READ) {
        if ((io->events & HV_READ) == 0) {
            hio_add(io, ssl_client_handshake, HV_READ);
        }
    }
    else {
        hloge("ssl handshake failed: %d", ret);
        hio_close(io);
    }
}

static void nio_accept(hio_t* io) {
    // printd("nio_accept listenfd=%d\n", io->fd);
    int connfd = 0, err = 0;
    socklen_t addrlen;
accept:
    addrlen = sizeof(sockaddr_u);
    connfd = accept(io->fd, io->peeraddr, &addrlen);
    hio_t* connio = NULL;
    if (connfd < 0) {
        err = socket_errno();
        if (err == EAGAIN) {
            //goto accept_done;
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
    if (io->unpack_setting) {
        hio_set_unpack(connio, io->unpack_setting);
    }

    if (io->io_type == HIO_TYPE_SSL) {
        if (connio->ssl == NULL) {
            hssl_ctx_t ssl_ctx = hssl_ctx_instance();
            if (ssl_ctx == NULL) {
                goto accept_error;
            }
            hssl_t ssl = hssl_new(ssl_ctx, connfd);
            if (ssl == NULL) {
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

    goto accept;

accept_error:
    hio_close(io);
}

static void nio_connect(hio_t* io) {
    // printd("nio_connect connfd=%d\n", io->fd);
    socklen_t addrlen = sizeof(sockaddr_u);
    int ret = getpeername(io->fd, io->peeraddr, &addrlen);
    if (ret < 0) {
        io->error = socket_errno();
        printd("connect failed: %s: %d\n", strerror(io->error), io->error);
        goto connect_failed;
    }
    else {
        addrlen = sizeof(sockaddr_u);
        getsockname(io->fd, io->localaddr, &addrlen);

        if (io->io_type == HIO_TYPE_SSL) {
            if (io->ssl == NULL) {
                hssl_ctx_t ssl_ctx = hssl_ctx_instance();
                if (ssl_ctx == NULL) {
                    goto connect_failed;
                }
                hssl_t ssl = hssl_new(ssl_ctx, io->fd);
                if (ssl == NULL) {
                    goto connect_failed;
                }
                io->ssl = ssl;
            }
            ssl_client_handshake(io);
        }
        else {
            // NOTE: SSL call connect_cb after handshake finished
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
    case HIO_TYPE_SSL:
        nread = hssl_read(io->ssl, buf, len);
        break;
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
    // hlogd("read retval=%d", nread);
    return nread;
}

static int __nio_write(hio_t* io, const void* buf, int len) {
    int nwrite = 0;
    switch (io->io_type) {
    case HIO_TYPE_SSL:
        nwrite = hssl_write(io->ssl, buf, len);
        break;
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
    // hlogd("write retval=%d", nwrite);
    return nwrite;
}

static void nio_read(hio_t* io) {
    // printd("nio_read fd=%d\n", io->fd);
    void* buf;
    int len = 0, nread = 0, err = 0;
read:
    buf = io->readbuf.base + io->readbuf.offset;
    if (io->read_until) {
        len = io->read_until;
    } else {
        len = io->readbuf.len - io->readbuf.offset;
    }
    nread = __nio_read(io, buf, len);
    // printd("read retval=%d\n", nread);
    if (nread < 0) {
        err = socket_errno();
        if (err == EAGAIN) {
            // goto read_done;
            return;
        } else if (err == EMSGSIZE) {
            // ignore
            return;
        } else {
            // perror("read");
            io->error = err;
            goto read_error;
        }
    }
    if (nread == 0) {
        goto disconnect;
    }
    if (io->read_until) {
        io->readbuf.offset += nread;
        io->read_until -= nread;
        if (io->read_until == 0) {
            __read_cb(io, io->readbuf.base, io->readbuf.offset);
            io->readbuf.offset = 0;
        }
    } else {
        __read_cb(io, buf, nread);
        if (nread == len) {
            goto read;
        }
    }
    return;
read_error:
disconnect:
    hio_close(io);
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
    char* buf = pbuf->base + pbuf->offset;
    int len = pbuf->len - pbuf->offset;
    nwrite = __nio_write(io, buf, len);
    // printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        err = socket_errno();
        if (err == EAGAIN) {
            //goto write_done;
            hrecursive_mutex_unlock(&io->write_mutex);
            return;
        } else {
            // perror("write");
            io->error = err;
            goto write_error;
        }
    }
    if (nwrite == 0) {
        goto disconnect;
    }
    __write_cb(io, buf, nwrite);
    pbuf->offset += nwrite;
    io->write_queue_bytes -= nwrite;
    if (nwrite == len) {
        HV_FREE(pbuf->base);
        write_queue_pop_front(&io->write_queue);
        // write next
        goto write;
    }
    hrecursive_mutex_unlock(&io->write_mutex);
    return;
write_error:
disconnect:
    hrecursive_mutex_unlock(&io->write_mutex);
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
        hrecursive_mutex_lock(&io->write_mutex);
        if (write_queue_empty(&io->write_queue)) {
            iowatcher_del_event(io->loop, io->fd, HV_WRITE);
            io->events &= ~HV_WRITE;
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
        nio_connect(io);
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
    return hio_add(io, hio_handle_events, HV_READ);
}

static void hio_write_event_cb(hevent_t* ev) {
    hio_t* io = (hio_t*)ev->userdata;
    if (io->closed) return;
    uint32_t id = (uintptr_t)ev->privdata;
    if (io->id != id) return;
    if (io->keepalive_timer) {
        htimer_reset(io->keepalive_timer);
    }
}

int hio_write (hio_t* io, const void* buf, size_t len) {
    if (io->closed) {
        hloge("hio_write called but fd[%d] already closed!", io->fd);
        return -1;
    }
    int nwrite = 0, err = 0;
    hrecursive_mutex_lock(&io->write_mutex);
    if (write_queue_empty(&io->write_queue)) {
try_write:
        nwrite = __nio_write(io, buf, len);
        // printd("write retval=%d\n", nwrite);
        if (nwrite < 0) {
            err = socket_errno();
            if (err == EAGAIN) {
                nwrite = 0;
                hlogw("try_write failed, enqueue!");
                goto enqueue;
            } else {
                // perror("write");
                io->error = err;
                goto write_error;
            }
        }
        if (nwrite == 0) {
            goto disconnect;
        }

        // __write_cb(io, buf, nwrite);
        if (io->keepalive_timer) {
            if (hv_gettid() == io->loop->tid) {
                htimer_reset(io->keepalive_timer);
            } else {
                hevent_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.cb = hio_write_event_cb;
                ev.userdata = io;
                ev.privdata = (void*)(uintptr_t)io->id;
                ev.priority = HEVENT_HIGH_PRIORITY;
                hloop_post_event(io->loop, &ev);
            }
        }
        if (io->write_cb) {
            // printd("write_cb------\n");
            io->write_cb(io, buf, nwrite);
            // printd("write_cb======\n");
        }

        if (nwrite == len) {
            //goto write_done;
            hrecursive_mutex_unlock(&io->write_mutex);
            return nwrite;
        }
enqueue:
        hio_add(io, hio_handle_events, HV_WRITE);
    }
    if (nwrite < len) {
        offset_buf_t remain;
        remain.len = len;
        remain.offset = nwrite;
        // NOTE: free in nio_write
        HV_ALLOC(remain.base, remain.len);
        memcpy(remain.base, buf, remain.len);
        if (io->write_queue.maxsize == 0) {
            write_queue_init(&io->write_queue, 4);
        }
        write_queue_push_back(&io->write_queue, &remain);
        io->write_queue_bytes += remain.len - remain.offset;
        if (io->write_queue_bytes > WRITE_QUEUE_HIGH_WATER) {
            hlogw("write queue %u, total %u, over high water %u",
                (unsigned int)(remain.len - remain.offset),
                (unsigned int)io->write_queue_bytes,
                (unsigned int)WRITE_QUEUE_HIGH_WATER);
        }
    }
    hrecursive_mutex_unlock(&io->write_mutex);
    return nwrite;
write_error:
disconnect:
    hrecursive_mutex_unlock(&io->write_mutex);
    hio_close(io);
    return nwrite;
}

int hio_close (hio_t* io) {
    if (io->closed) return 0;
    if (hv_gettid() != io->loop->tid) {
        return hio_close_async(io);
    }
    hrecursive_mutex_lock(&io->write_mutex);
    if (!write_queue_empty(&io->write_queue) && io->error == 0 && io->close == 0) {
        hrecursive_mutex_unlock(&io->write_mutex);
        io->close = 1;
        hlogw("write_queue not empty, close later.");
        int timeout_ms = io->close_timeout ? io->close_timeout : HIO_DEFAULT_CLOSE_TIMEOUT;
        io->close_timer = htimer_add(io->loop, __close_timeout_cb, timeout_ms, 1);
        io->close_timer->privdata = io;
        return 0;
    }

    io->closed = 1;
    hio_done(io);
    __close_cb(io);
    if (io->ssl) {
        hssl_free(io->ssl);
        io->ssl = NULL;
    }
    if (io->io_type & HIO_TYPE_SOCKET) {
        closesocket(io->fd);
    }
    hrecursive_mutex_unlock(&io->write_mutex);
    return 0;
}
#endif
