#include "hevent.h"
#include "hsocket.h"
#include "hatomic.h"
#include "hlog.h"

uint64_t hloop_next_event_id() {
    static hatomic_t s_id = HATOMIC_VAR_INIT(0);
    return ++s_id;
}

uint32_t hio_next_id() {
    static hatomic_t s_id = HATOMIC_VAR_INIT(0);
    return ++s_id;
}

uint32_t hio_id (hio_t* io) {
    return io->id;
}

int hio_fd(hio_t* io) {
    return io->fd;
}

hio_type_e hio_type(hio_t* io) {
    return io->io_type;
}

int hio_error(hio_t* io) {
    return io->error;
}

int hio_events(hio_t* io) {
    return io->events;
}

int hio_revents(hio_t* io) {
    return io->revents;
}

struct sockaddr* hio_localaddr(hio_t* io) {
    return io->localaddr;
}

struct sockaddr* hio_peeraddr(hio_t* io) {
    return io->peeraddr;
}

void hio_set_context(hio_t* io, void* ctx) {
    io->ctx = ctx;
}

void* hio_context(hio_t* io) {
    return io->ctx;
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

void hio_set_keepalive_timeout(hio_t* io, int timeout_ms) {
    if (io->keepalive_timer) {
        if (timeout_ms == 0) {
            htimer_del(io->keepalive_timer);
            io->keepalive_timer = NULL;
        } else {
            ((struct htimeout_s*)io->keepalive_timer)->timeout = timeout_ms;
            htimer_reset(io->keepalive_timer);
        }
    } else {
        io->keepalive_timer = htimer_add(io->loop, __keepalive_timeout_cb, timeout_ms, 1);
        io->keepalive_timer->privdata = io;
    }
    io->keepalive_timeout = timeout_ms;
}

static void __heartbeat_timer_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io && io->heartbeat_fn) {
        io->heartbeat_fn(io);
    }
}

void hio_set_heartbeat(hio_t* io, int interval_ms, hio_send_heartbeat_fn fn) {
    if (io->heartbeat_timer) {
        if (interval_ms == 0) {
            htimer_del(io->heartbeat_timer);
            io->heartbeat_timer = NULL;
        } else {
            ((struct htimeout_s*)io->heartbeat_fn)->timeout = interval_ms;
            htimer_reset(io->keepalive_timer);
        }
    } else {
        io->heartbeat_timer = htimer_add(io->loop, __heartbeat_timer_cb, interval_ms, INFINITE);
        io->heartbeat_timer->privdata = io;
    }
    io->heartbeat_interval = interval_ms;
    io->heartbeat_fn = fn;
}
