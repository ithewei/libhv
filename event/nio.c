#include "iowatcher.h"
#ifndef EVENT_IOCP
#include "hio.h"
#include "hsocket.h"

static void nio_accept(hio_t* io) {
    //printd("nio_accept listenfd=%d\n", io->fd);
    socklen_t addrlen;
accept:
    addrlen = sizeof(struct sockaddr_in6);
    int connfd = accept(io->fd, io->peeraddr, &addrlen);
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
    addrlen = sizeof(struct sockaddr_in6);
    getsockname(connfd, io->localaddr, &addrlen);

    if (io->accept_cb) {
        char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
        char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
        printd("accept listenfd=%d connfd=%d [%s] <= [%s]\n", io->fd, connfd,
                sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
                sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
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
    socklen_t addrlen = sizeof(struct sockaddr_in6);
    int ret = getpeername(io->fd, io->peeraddr, &addrlen);
    if (ret < 0) {
        io->error = socket_errno();
        printd("connect failed: %s: %d\n", strerror(socket_errno()), socket_errno());
        state = 0;
    }
    else {
        addrlen = sizeof(struct sockaddr_in6);
        getsockname(io->fd, io->localaddr, &addrlen);
        char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
        char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
        printd("connect connfd=%d [%s] => [%s]\n", io->fd,
                sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
                sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
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
    switch (io->io_type) {
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
        socklen_t addrlen = sizeof(struct sockaddr_in6);
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
    switch (io->io_type) {
    case HIO_TYPE_TCP:
#ifdef OS_UNIX
        nwrite = write(io->fd, buf, len);
#else
        nwrite = send(io->fd, buf, len, 0);
#endif
        break;
    case HIO_TYPE_UDP:
    case HIO_TYPE_IP:
        nwrite = sendto(io->fd, buf, len, 0, io->peeraddr, sizeof(struct sockaddr_in6));
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
            io->connect = 0;

            nio_connect(io);
        }
        else {
            nio_write(io);
        }
        // NOTE: del WRITE_EVENT, if write_queue empty
        if (write_queue_empty(&io->write_queue)) {
            hio_del(io, WRITE_EVENT);
        }
    }

    io->revents = 0;
}

int hio_accept(hio_t* io) {
    hio_add(io, hio_handle_events, READ_EVENT);
    return 0;
}

int hio_connect(hio_t* io) {
    int ret = connect(io->fd, io->peeraddr, sizeof(struct sockaddr_in6));
#ifdef OS_WIN
    if (ret < 0 && socket_errno() != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && socket_errno() != EINPROGRESS) {
#endif
        perror("connect");
        hclose(io);
        return ret;
    }
    return hio_add(io, hio_handle_events, WRITE_EVENT);
}

int hio_read (hio_t* io) {
    return hio_add(io, hio_handle_events, READ_EVENT);
}

int hio_write (hio_t* io, const void* buf, size_t len) {
    int nwrite = 0;
    if (write_queue_empty(&io->write_queue)) {
try_write:
        switch (io->io_type) {
        case HIO_TYPE_TCP:
#ifdef OS_UNIX
            nwrite = write(io->fd, buf, len);
#else
            nwrite = send(io->fd, buf, len, 0);
#endif
            break;
        case HIO_TYPE_UDP:
        case HIO_TYPE_IP:
            nwrite = sendto(io->fd, buf, len, 0, io->peeraddr, sizeof(struct sockaddr_in6));
            break;
        default:
            nwrite = write(io->fd, buf, len);
            break;
        }
        //printd("write retval=%d\n", nwrite);
        if (nwrite < 0) {
            if (socket_errno() == EAGAIN) {
                nwrite = 0;
                goto enqueue;
            }
            else {
                perror("write");
                io->error = socket_errno();
                goto write_error;
            }
        }
        if (nwrite == 0) {
            goto disconnect;
        }
        if (io->write_cb) {
            printd("try_write_cb------\n");
            io->write_cb(io, buf, nwrite);
            printd("try_write_cb======\n");
        }
        if (nwrite == len) {
            //goto write_done;
            return nwrite;
        }
        hio_add(io, hio_handle_events, WRITE_EVENT);
    }
enqueue:
    if (nwrite < len) {
        offset_buf_t rest;
        rest.len = len;
        rest.offset = nwrite;
        // NOTE: free in nio_write
        SAFE_ALLOC(rest.base, rest.len);
        memcpy(rest.base, (char*)buf, rest.len);
        if (io->write_queue.maxsize == 0) {
            write_queue_init(&io->write_queue, 4);
        }
        write_queue_push_back(&io->write_queue, &rest);
    }
    return nwrite;
write_error:
disconnect:
    hclose(io);
    return nwrite;
}

int hio_close (hio_t* io) {
#ifdef OS_UNIX
    close(io->fd);
#else
    closesocket(io->fd);
#endif
    return 0;
}
#endif
