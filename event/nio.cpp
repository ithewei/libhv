#include "hloop.h"
#include "hio.h"
#include "hsocket.h"

static void on_accept(hio_t* io, void* userdata) {
    //printf("on_accept listenfd=%d\n", io->fd);
    struct sockaddr_in peeraddr;
    socklen_t addrlen;
    //struct sockaddr_in localaddr;
    //addrlen = sizeof(struct sockaddr_in);
    //getsockname(io->fd, (struct sockaddr*)&localaddr, &addrlen);
accept:
    addrlen = sizeof(struct sockaddr_in);
    int connfd = accept(io->fd, (struct sockaddr*)&peeraddr, &addrlen);
    if (connfd < 0) {
        if (sockerrno == NIO_EAGAIN) {
            //goto accept_done;
            return;
        }
        else {
            perror("accept");
            goto accept_error;
        }
    }
    //printf("accept connfd=%d [%s:%d] <= [%s:%d]\n", connfd,
            //inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port),
            //inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

    if (io->accept_cb) {
        io->accept_cb(io, connfd, io->accept_userdata);
    }

    goto accept;

accept_error:
    hclose(io);
}

static void on_connect(hio_t* io, void* userdata) {
    //printf("on_connect connfd=%d\n", io->fd);
    int state = 0;
    struct sockaddr_in peeraddr;
    socklen_t addrlen;
    addrlen = sizeof(struct sockaddr_in);
    int ret = getpeername(io->fd, (struct sockaddr*)&peeraddr, &addrlen);
    if (ret < 0) {
        //printf("connect failed: %s: %d\n", strerror(sockerrno), sockerrno);
        state = 0;
    }
    else {
        //struct sockaddr_in localaddr;
        //addrlen = sizeof(struct sockaddr_in);
        //getsockname(ioent->fd, (struct sockaddr*)&localaddr, &addrlen);
        //printf("connect connfd=%d [%s:%d] => [%s:%d]\n", io->fd,
                //inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port),
                //inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));
        state = 1;
    }
    if (io->connect_cb) {
        io->connect_cb(io, state, io->connect_userdata);
    }
}

static void on_readable(hio_t* io, void* userdata) {
    //printf("on_read fd=%d\n", io->fd);
    int nread;
    void* buf = io->readbuf;
    int   len = io->readbuflen;
read:
    memset(buf, 0, len);
    nread = read(io->fd, buf, len);
    //printf("read retval=%d\n", nread);
    if (nread < 0) {
        if (sockerrno == NIO_EAGAIN) {
            //goto read_done;
            return;
        }
        else {
            perror("read");
            goto read_error;
        }
    }
    if (nread == 0) {
        goto disconnect;
    }
    //printf("> %s\n", buf);
    if (io->read_cb) {
        io->read_cb(io, io->readbuf, nread, io->read_userdata);
    }
    if (nread == len) {
        goto read;
    }
    return;
read_error:
disconnect:
    hclose(io);
}

static void on_writeable(hio_t* io, void* userdata) {
    printf("on_write fd=%d\n", io->fd);
    /*
    int nwrite;
write:
    if (io->write_queue.empty()) {
        return;
    }
    pbuf = io->write_queue.front();
    nwrite = write(ioent->fd, buf, len);
    if (nwrite < 0) {
        if (nwrite == NIO_EAGAIN) {
            //goto write_done;
            return;
        }
        else {
        }
    }
    if (io->write_cb) {
        io->write_cb(io, nwrite);
    }
    if (nwrite == len) {
        io->write_queue.pop_front();
        goto write;
    }
    //pbuf->buf += nwrite;
    return;
write_error:
disconnect:
    hclose(ioent);
    */
}

hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb, void* accept_userdata,
                    hclose_cb close_cb, void* close_userdata) {
    hio_t* io = hio_accept(loop, listenfd, on_accept, NULL);
    if (io) {
        io->accept_cb = accept_cb;
        io->accept_userdata = accept_userdata;
        if (close_cb) {
            io->close_cb = close_cb;
        }
        if (close_userdata) {
            io->close_userdata = close_userdata;
        }
    }
    return io;
}

hio_t* hconnect (hloop_t* loop, int connfd, hconnect_cb connect_cb, void* connect_userdata,
                    hclose_cb close_cb, void* close_userdata) {
    hio_t* io = hio_connect(loop, connfd, on_connect, NULL);
    if (io) {
        io->connect_cb = connect_cb;
        io->connect_userdata = connect_userdata;
        if (close_cb) {
            io->close_cb = close_cb;
        }
        if (close_userdata) {
            io->close_userdata = close_userdata;
        }
    }
    return io;
}

hio_t* hread    (hloop_t* loop, int fd, void* readbuf, size_t readbuflen, hread_cb read_cb, void* read_userdata,
                    hclose_cb close_cb, void* close_userdata) {
    hio_t* io = hio_read(loop, fd, on_readable, NULL);
    if (io) {
        io->readbuf = (char*)readbuf;
        io->readbuflen = readbuflen;
        io->read_cb = read_cb;
        io->read_userdata = read_userdata;
        if (close_cb) {
            io->close_cb = close_cb;
        }
        if (close_userdata) {
            io->close_userdata = close_userdata;
        }
    }
    return io;
}

hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb, void* write_userdata,
                    hclose_cb close_cb, void* close_userdata) {
    hio_t* io = hio_add(loop, fd);
    if (io == NULL) return NULL;
    io->write_cb = write_cb;
    io->write_userdata = write_userdata;
    if (close_cb) {
        io->close_cb = close_cb;
    }
    if (close_userdata) {
        io->close_userdata = close_userdata;
    }
    int nwrite;
    if (1) {
    //if (io->write_queue.empty()) {
try_write:
        nwrite = write(fd, buf, len);
        if (nwrite < 0) {
            if (sockerrno == NIO_EAGAIN) {
                nwrite = 0;
                goto push_queue;
            }
            else {
                perror("write");
                goto write_error;
            }
            goto write_error;
        }
        if (nwrite == 0) {
            goto disconnect;
        }
        if (write_cb) {
            write_cb(io, buf, nwrite, io->write_userdata);
        }
        if (nwrite == len) {
            //goto write_done;
            return io;
        }
    }
push_queue:
    printf("write retval=%d buflen=%ld\n", nwrite, len);
    //ioent->write_queue.push(buf+nwrite, len-nwrite);
    //hioent_write(loop, fd, on_writeable, NULL);
    return io;
write_error:
disconnect:
    hclose(io);
    return io;
}

void hclose(hio_t* io) {
    //printf("close fd=%d\n", io->fd);
    close(io->fd);
    if (io->close_cb) {
        io->close_cb(io, io->close_userdata);
    }
    hio_del(io);
}
