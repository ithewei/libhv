#include "iowatcher.h"
#ifndef EVENT_IOCP
#include "hsocket.h"

static void nio_accept(hio_t* io) {
    //printd("nio_accept listenfd=%d\n", io->fd);
    socklen_t addrlen;
    if (io->localaddr == NULL) {
        io->localaddr = (struct sockaddr*)malloc(sizeof(struct sockaddr_in));
    }
    if (io->peeraddr == NULL) {
        io->peeraddr = (struct sockaddr*)malloc(sizeof(struct sockaddr_in));
    }
accept:
    addrlen = sizeof(struct sockaddr_in);
    int connfd = accept(io->fd, io->peeraddr, &addrlen);
    if (connfd < 0) {
        if (sockerrno == NIO_EAGAIN) {
            //goto accept_done;
            return;
        }
        else {
            io->error = sockerrno;
            perror("accept");
            goto accept_error;
        }
    }

    addrlen = sizeof(struct sockaddr_in);
    getsockname(connfd, io->localaddr, &addrlen);
    if (io->accept_cb) {
        struct sockaddr_in* localaddr = (struct sockaddr_in*)io->localaddr;
        struct sockaddr_in* peeraddr = (struct sockaddr_in*)io->peeraddr;
        char localip[64];
        char peerip[64];
        inet_ntop(AF_INET, &localaddr->sin_addr, localip, sizeof(localip));
        inet_ntop(AF_INET, &peeraddr->sin_addr, peerip, sizeof(peerip));
        printd("accept listenfd=%d connfd=%d [%s:%d] <= [%s:%d]\n", io->fd, connfd,
                localip, ntohs(localaddr->sin_port),
                peerip, ntohs(peeraddr->sin_port));
        printd("accept_cb------\n");
        io->accept_cb(io, connfd);
        printd("accept_cb======\n");
    }

    goto accept;

accept_error:
    hclose(io);
}

static void nio_connect(hio_t* io) {
    //printd("nio_connect connfd=%d\n", io->fd);
    int state = 0;
    socklen_t addrlen;
    if (io->localaddr == NULL) {
        io->localaddr = (struct sockaddr*)malloc(sizeof(struct sockaddr_in));
        addrlen = sizeof(struct sockaddr_in);
        getsockname(io->fd, io->localaddr, &addrlen);
    }
    if (io->peeraddr == NULL) {
        io->peeraddr = (struct sockaddr*)malloc(sizeof(struct sockaddr_in));
    }
    addrlen = sizeof(struct sockaddr_in);
    int ret = getpeername(io->fd, io->peeraddr, &addrlen);
    if (ret < 0) {
        io->error = sockerrno;
        printd("connect failed: %s: %d\n", strerror(sockerrno), sockerrno);
        state = 0;
    }
    else {
        struct sockaddr_in* localaddr = (struct sockaddr_in*)io->localaddr;
        struct sockaddr_in* peeraddr = (struct sockaddr_in*)io->peeraddr;
        char localip[64];
        char peerip[64];
        inet_ntop(AF_INET, &localaddr->sin_addr, localip, sizeof(localip));
        inet_ntop(AF_INET, &peeraddr->sin_addr, peerip, sizeof(peerip));
        printd("connect connfd=%d [%s:%d] => [%s:%d]\n", io->fd,
                localip, ntohs(localaddr->sin_port),
                peerip, ntohs(peeraddr->sin_port));
        state = 1;
    }
    if (io->connect_cb) {
        printd("connect_cb------\n");
        io->connect_cb(io, state);
        printd("connect_cb======\n");
    }
    if (state == 0) {
        hclose(io);
    }
}

static void nio_read(hio_t* io) {
    //printd("nio_read fd=%d\n", io->fd);
    int nread;
    void* buf = io->readbuf.base;
    int   len = io->readbuf.len;
read:
    memset(buf, 0, len);
#ifdef OS_UNIX
    nread = read(io->fd, buf, len);
#else
    nread = recv(io->fd, buf, len, 0);
#endif
    //printd("read retval=%d\n", nread);
    if (nread < 0) {
        if (sockerrno == NIO_EAGAIN) {
            //goto read_done;
            return;
        }
        else {
            io->error = sockerrno;
            perror("read");
            goto read_error;
        }
    }
    if (nread == 0) {
        goto disconnect;
    }
    //printd("> %s\n", buf);
    if (io->read_cb) {
        printd("read_cb------\n");
        io->read_cb(io, buf, nread);
        printd("read_cb======\n");
    }
    if (nread == len) {
        goto read;
    }
    return;
read_error:
disconnect:
    hclose(io);
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
#ifdef OS_UNIX
    nwrite = write(io->fd, buf, len);
#else
    nwrite = send(io->fd, buf, len, 0);
#endif
    //printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        if (sockerrno == NIO_EAGAIN) {
            //goto write_done;
            return;
        }
        else {
            io->error = sockerrno;
            perror("write");
            goto write_error;
        }
    }
    if (nwrite == 0) {
        goto disconnect;
    }
    if (io->write_cb) {
        printd("write_cb------\n");
        io->write_cb(io, buf, nwrite);
        printd("write_cb======\n");
    }
    pbuf->offset += nwrite;
    if (nwrite == len) {
        SAFE_FREE(pbuf->base);
        write_queue_pop_front(&io->write_queue);
        // write next
        goto write;
    }
    return;
write_error:
disconnect:
    hclose(io);
}

static void hio_handle_events(hio_t* io) {
    if ((io->events & READ_EVENT) && (io->revents & READ_EVENT)) {
        if (io->accept) {
            nio_accept(io);
        }
        else {
            nio_read(io);
        }
    }

    if ((io->events & WRITE_EVENT) && (io->revents & WRITE_EVENT)) {
        if (io->connect) {
            // NOTE: connect just do once
            // ONESHOT
            hio_del(io, WRITE_EVENT);
            io->connect = 0;

            nio_connect(io);
        }
        else {
            nio_write(io);
            // NOTE: del WRITE_EVENT, if write_queue empty
            if (write_queue_empty(&io->write_queue)) {
                hio_del(io, WRITE_EVENT);
            }
        }
    }

    io->revents = 0;
}

hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb) {
    hio_t* io = hio_add(loop, hio_handle_events, listenfd, READ_EVENT);
    if (io == NULL) return NULL;
    if (accept_cb) {
        io->accept_cb = accept_cb;
    }
    io->accept = 1;
    nonblocking(listenfd);
    return io;
}

hio_t* hconnect (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb) {
    int connfd = Connect(host, port, 1);
    if (connfd < 0) {
        return NULL;
    }
    hio_t* io = hio_add(loop, hio_handle_events, connfd, WRITE_EVENT);
    if (io == NULL) {
        closesocket(connfd);
        return NULL;
    }
    if (connect_cb) {
        io->connect_cb = connect_cb;
    }
    io->connect = 1;
    nonblocking(connfd);
    return io;
}

hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb) {
    hio_t* io = hio_add(loop, hio_handle_events, fd, READ_EVENT);
    if (io == NULL) return NULL;
    io->readbuf.base = (char*)buf;
    io->readbuf.len = len;
    if (read_cb) {
        io->read_cb = read_cb;
    }
    return io;
}

hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb) {
    hio_t* io = hio_add(loop, hio_handle_events, fd, 0);
    if (io == NULL) return NULL;
    if (write_cb) {
        io->write_cb = write_cb;
    }
    int nwrite = 0;
    if (write_queue_empty(&io->write_queue)) {
try_write:
#ifdef OS_UNIX
        nwrite = write(fd, buf, len);
#else
        nwrite = send(fd, buf, len, 0);
#endif
        //printd("write retval=%d\n", nwrite);
        if (nwrite < 0) {
            if (sockerrno == NIO_EAGAIN) {
                nwrite = 0;
                goto enqueue;
            }
            else {
                perror("write");
                io->error = sockerrno;
                goto write_error;
            }
        }
        if (nwrite == 0) {
            goto disconnect;
        }
        if (write_cb) {
            printd("try_write_cb------\n");
            write_cb(io, buf, nwrite);
            printd("try_write_cb======\n");
        }
        if (nwrite == len) {
            //goto write_done;
            return io;
        }
        hio_add(loop, hio_handle_events, fd, WRITE_EVENT);
    }
enqueue:
    if (nwrite < len) {
        offset_buf_t rest;
        rest.len = len;
        rest.offset = nwrite;
        // NOTE: free in nio_write;
        rest.base = (char*)malloc(rest.len);
        if (rest.base == NULL) return io;
        memcpy(rest.base, (char*)buf, rest.len);
        if (io->write_queue.maxsize == 0) {
            write_queue_init(&io->write_queue, 4);
        }
        write_queue_push_back(&io->write_queue, &rest);
    }
    return io;
write_error:
disconnect:
    hclose(io);
    return io;
}

void   hclose   (hio_t* io) {
    //printd("close fd=%d\n", io->fd);
    if (io->closed) return;
#ifdef OS_UNIX
    close(io->fd);
#else
    closesocket(io->fd);
#endif
    io->closed = 1;
    if (io->close_cb) {
        printd("close_cb------\n");
        io->close_cb(io);
        printd("close_cb======\n");
    }
    hio_del(io, ALL_EVENTS);
}
#endif
