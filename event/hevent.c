#include "hevent.h"
#include "hsocket.h"

int hio_fd(hio_t* io) {
    return io->fd;
}

hio_type_e hio_type(hio_t* io) {
    return io->io_type;
}

int hio_error(hio_t* io) {
    return io->error;
}

struct sockaddr* hio_localaddr(hio_t* io) {
    return io->localaddr;
}

struct sockaddr* hio_peeraddr(hio_t* io) {
    return io->peeraddr;
}

void hio_setcb_accept   (hio_t* io, haccept_cb  accept_cb) {
    io->accept_cb = accept_cb;
}

void hio_setcb_connect  (hio_t* io, hconnect_cb connect_cb) {
    io->connect_cb = connect_cb;
}

void hio_setcb_read     (hio_t* io, hread_cb    read_cb) {
    io->read_cb = read_cb;
}

void hio_setcb_write    (hio_t* io, hwrite_cb   write_cb) {
    io->write_cb = write_cb;
}

void hio_setcb_close    (hio_t* io, hclose_cb   close_cb) {
    io->close_cb = close_cb;
}

void hio_set_type(hio_t* io, hio_type_e type) {
    io->io_type = type;
}

void hio_set_localaddr(hio_t* io, struct sockaddr* addr, int addrlen) {
    if (io->localaddr == NULL) {
        HV_ALLOC(io->localaddr, sizeof(sockaddr_u));
    }
    memcpy(io->localaddr, addr, addrlen);
}

void hio_set_peeraddr (hio_t* io, struct sockaddr* addr, int addrlen) {
    if (io->peeraddr == NULL) {
        HV_ALLOC(io->peeraddr, sizeof(sockaddr_u));
    }
    memcpy(io->peeraddr, addr, addrlen);
}

int hio_enable_ssl(hio_t* io) {
    io->io_type = HIO_TYPE_SSL;
    return 0;
}

void hio_set_readbuf(hio_t* io, void* buf, size_t len) {
    if (buf == NULL || len == 0) {
        hloop_t* loop = io->loop;
        if (loop && (loop->readbuf.base == NULL || loop->readbuf.len == 0)) {
            loop->readbuf.len = HLOOP_READ_BUFSIZE;
            HV_ALLOC(loop->readbuf.base, loop->readbuf.len);
            io->readbuf = loop->readbuf;
        }
    }
    else {
        io->readbuf.base = (char*)buf;
        io->readbuf.len = len;
    }
}

void hio_set_connect_timeout(hio_t* io, int timeout_ms) {
    io->connect_timeout = timeout_ms;
}

void hio_set_close_timeout(hio_t* io, int timeout_ms) {
    io->close_timeout = timeout_ms;
}

void hio_set_keepalive_timeout(hio_t* io, int timeout_ms) {
    io->keepalive_timeout = timeout_ms;
}

void hio_set_heartbeat(hio_t* io, int interval_ms, hio_send_heartbeat_fn fn) {
    io->heartbeat_interval = interval_ms;
    io->heartbeat_fn = fn;
}
