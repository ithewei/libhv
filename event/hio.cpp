#include "hio.h"
#include "io_watcher.h"

void hio_init(hio_t* io) {
    hloop_t* loop = io->loop;
    int fd = io->fd;
    memset(io, 0, sizeof(hio_t));
    io->event_index[0] = io->event_index[1] = -1;
    io->loop = loop;
    io->fd = fd;
}

hio_t* hio_get(hloop_t* loop, int fd) {
    auto iter = loop->ios.find(fd);
    if (iter == loop->ios.end()) {
        return NULL;
    }
    return iter->second;
}

hio_t* hio_add(hloop_t* loop, int fd) {
    // first try get
    hio_t* io = hio_get(loop, fd);
    if (io == NULL) {
        // then add
#ifdef EVENT_SELECT
        if (loop->ios.size() >= FD_SETSIZE) return NULL;
#endif
        io = (hio_t*)malloc(sizeof(hio_t));
        hio_init(io);
        io->event_type= HEVENT_TYPE_IO;
        io->event_id = ++loop->event_counter;
        loop->ios[fd] = io;
    }
    io->loop = loop;
    io->fd = fd;
    io->active = 1;
    return io;
}

void hio_del(hio_t* io) {
    iowatcher_del_event(io, ALL_EVENTS);
    io->events = 0;
     // no free, just init for reuse
    hio_init(io);
}

hio_t* hio_read (hloop_t* loop, int fd, hio_cb revent_cb, void* revent_userdata) {
    hio_t* io = hio_add(loop, fd);
    if (io == NULL) return NULL;
    io->revent_cb = revent_cb;
    io->revent_userdata = revent_userdata;
    iowatcher_add_event(io, READ_EVENT);
    io->events |= READ_EVENT;
    return io;
}

hio_t* hio_write  (hloop_t* loop, int fd, hio_cb wevent_cb, void* wevent_userdata) {
    hio_t* io = hio_add(loop, fd);
    if (io == NULL) return NULL;
    io->wevent_cb = wevent_cb;
    io->wevent_userdata = wevent_userdata;
    iowatcher_add_event(io, WRITE_EVENT);
    io->events |= WRITE_EVENT;
    return io;
}

#include "hsocket.h"
hio_t* hio_accept (hloop_t* loop, int listenfd, hio_cb revent_cb, void* revent_userdata) {
    hio_t* io = hio_read(loop, listenfd, revent_cb, revent_userdata);
    if (io) {
        nonblocking(listenfd);
        io->accept = 1;
    }
    return io;
}

hio_t* hio_connect(hloop_t* loop, int connfd, hio_cb wevent_cb, void* wevent_userdata) {
    hio_t* io = hio_write(loop, connfd, wevent_cb, wevent_userdata);
    if (io) {
        nonblocking(connfd);
        io->connect = 1;
    }
    return io;
}

static int handle_read_event(hio_t* io) {
    if (!io->active) return 0;
    if (io->revent_cb) {
        io->revent_cb(io, io->revent_userdata);
    }
    return 0;
}

static int handle_write_event(hio_t* io) {
    if (!io->active) return 0;
    bool connect_event = io->connect;
    if (connect_event) {
        // ONESHOT
        iowatcher_del_event(io, WRITE_EVENT);
        io->connect = 0;
    }
    if (io->wevent_cb) {
        io->wevent_cb(io, io->wevent_userdata);
    }
    //if (!connect_event && io->write_queue.empty()) {
        //iowatcher_del_event(io, WRITE_EVENT);
    //}
    return 0;
}

int hio_handle_events(hio_t* io) {
    if (io->revents & READ_EVENT) {
        handle_read_event(io);
    }
    if (io->revents & WRITE_EVENT) {
        handle_write_event(io);
    }
    io->revents = 0;
    return 0;
}
