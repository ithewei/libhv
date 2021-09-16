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

haccept_cb  hio_getcb_accept(hio_t* io) {
    return io->accept_cb;
}

hconnect_cb hio_getcb_connect(hio_t* io) {
    return io->connect_cb;
}

hread_cb    hio_getcb_read(hio_t* io) {
    return io->read_cb;
}

hwrite_cb   hio_getcb_write(hio_t* io) {
    return io->write_cb;
}

hclose_cb   hio_getcb_close(hio_t* io) {
    return io->close_cb;
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

bool hio_is_ssl(hio_t* io) {
    return io->io_type == HIO_TYPE_SSL;
}

hssl_t hio_get_ssl(hio_t* io) {
    return io->ssl;
}

int hio_set_ssl(hio_t* io, hssl_t ssl) {
    io->io_type = HIO_TYPE_SSL;
    io->ssl = ssl;
    return 0;
}

void hio_set_readbuf(hio_t* io, void* buf, size_t len) {
    assert(io && buf && len != 0);
    io->readbuf.base = (char*)buf;
    io->readbuf.len = len;
    io->readbuf.offset = 0;
}

void hio_del_connect_timer(hio_t* io) {
    if (io->connect_timer) {
        htimer_del(io->connect_timer);
        io->connect_timer = NULL;
        io->connect_timeout = 0;
    }
}

void hio_del_close_timer(hio_t* io) {
    if (io->close_timer) {
        htimer_del(io->close_timer);
        io->close_timer = NULL;
        io->close_timeout = 0;
    }
}

void hio_del_keepalive_timer(hio_t* io) {
    if (io->keepalive_timer) {
        htimer_del(io->keepalive_timer);
        io->keepalive_timer = NULL;
        io->keepalive_timeout = 0;
    }
}

void hio_del_heartbeat_timer(hio_t* io) {
    if (io->heartbeat_timer) {
        htimer_del(io->heartbeat_timer);
        io->heartbeat_timer = NULL;
        io->heartbeat_interval = 0;
        io->heartbeat_fn = NULL;
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
    if (timeout_ms == 0) {
        // del
        hio_del_keepalive_timer(io);
        return;
    }

    if (io->keepalive_timer) {
        // reset
        ((struct htimeout_s*)io->keepalive_timer)->timeout = timeout_ms;
        htimer_reset(io->keepalive_timer);
    } else {
        // add
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
    if (interval_ms == 0) {
        // del
        hio_del_heartbeat_timer(io);
        return;
    }

    if (io->heartbeat_timer) {
        // reset
        ((struct htimeout_s*)io->heartbeat_timer)->timeout = interval_ms;
        htimer_reset(io->heartbeat_timer);
    } else {
        // add
        io->heartbeat_timer = htimer_add(io->loop, __heartbeat_timer_cb, interval_ms, INFINITE);
        io->heartbeat_timer->privdata = io;
    }
    io->heartbeat_interval = interval_ms;
    io->heartbeat_fn = fn;
}

bool hio_is_alloced_readbuf(hio_t* io) {
    return  io->alloced_readbuf &&
            io->readbuf.base &&
            io->readbuf.len &&
            io->readbuf.base != io->loop->readbuf.base;
}

void hio_alloc_readbuf(hio_t* io, int len) {
    if (hio_is_alloced_readbuf(io)) {
        io->readbuf.base = (char*)safe_realloc(io->readbuf.base, len, io->readbuf.len);
    } else {
        HV_ALLOC(io->readbuf.base, len);
    }
    io->readbuf.len = len;
    io->alloced_readbuf = 1;
}

void hio_free_readbuf(hio_t* io) {
    if (hio_is_alloced_readbuf(io)) {
        HV_FREE(io->readbuf.base);
        io->alloced_readbuf = 0;
        // reset to loop->readbuf
        io->readbuf.base = io->loop->readbuf.base;
        io->readbuf.len = io->loop->readbuf.len;
    }
}

void hio_unset_unpack(hio_t* io) {
    if (io->unpack_setting) {
        io->unpack_setting = NULL;
        // NOTE: unpack has own readbuf
        hio_free_readbuf(io);
    }
}

void hio_set_unpack(hio_t* io, unpack_setting_t* setting) {
    hio_unset_unpack(io);
    if (setting == NULL) return;

    io->unpack_setting = setting;
    if (io->unpack_setting->package_max_length == 0) {
        io->unpack_setting->package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
    }
    if (io->unpack_setting->mode == UNPACK_BY_FIXED_LENGTH) {
        assert(io->unpack_setting->fixed_length != 0 &&
               io->unpack_setting->fixed_length <= io->unpack_setting->package_max_length);
    }
    else if (io->unpack_setting->mode == UNPACK_BY_DELIMITER) {
        if (io->unpack_setting->delimiter_bytes == 0) {
            io->unpack_setting->delimiter_bytes = strlen((char*)io->unpack_setting->delimiter);
        }
    }
    else if (io->unpack_setting->mode == UNPACK_BY_LENGTH_FIELD) {
        assert(io->unpack_setting->body_offset >=
               io->unpack_setting->length_field_offset +
               io->unpack_setting->length_field_bytes);
    }

    // NOTE: unpack must have own readbuf
    if (io->unpack_setting->mode == UNPACK_BY_FIXED_LENGTH) {
        io->readbuf.len = io->unpack_setting->fixed_length;
    } else {
        io->readbuf.len = HLOOP_READ_BUFSIZE;
    }
    hio_alloc_readbuf(io, io->readbuf.len);
}
