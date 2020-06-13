#include "iowatcher.h"

#ifdef EVENT_PORT

#include "hplatform.h"
#include "hdef.h"
#include "hevent.h"

#include <port.h>

#define EVENTS_INIT_SIZE     64

typedef struct evport_ctx_s {
    int port;
    int capacity;
    int nevents;
    port_event_t* events;
} evport_ctx_t;

static void evport_ctx_resize(evport_ctx_t* evport_ctx, int size) {
    int bytes = sizeof(port_event_t) * size;
    int oldbytes = sizeof(port_event_t) * evport_ctx->capacity;
    evport_ctx->events = (port_event_t*)safe_realloc(evport_ctx->events, bytes, oldbytes);
    evport_ctx->capacity = size;
}

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher) return 0;
    evport_ctx_t* evport_ctx;
    HV_ALLOC_SIZEOF(evport_ctx);
    evport_ctx->port = port_create();
    evport_ctx->capacity = EVENTS_INIT_SIZE;
    evport_ctx->nevents = 0;
    int bytes = sizeof(port_event_t) * evport_ctx->capacity;
    HV_ALLOC(evport_ctx->events, bytes);
    loop->iowatcher = evport_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    evport_ctx_t* evport_ctx = (evport_ctx_t*)loop->iowatcher;
    close(evport_ctx->port);
    HV_FREE(evport_ctx->events);
    HV_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    evport_ctx_t* evport_ctx = (evport_ctx_t*)loop->iowatcher;
    hio_t* io = loop->ios.ptr[fd];

    int evport_events = 0;
    if (io->events & HV_READ) {
        evport_events |= POLLIN;
    }
    if (io->events & HV_WRITE) {
        evport_events |= POLLOUT;
    }
    if (events & HV_READ) {
        evport_events |= POLLIN;
    }
    if (events & HV_WRITE) {
        evport_events |= POLLOUT;
    }
    port_associate(evport_ctx->port, PORT_SOURCE_FD, fd, evport_events, NULL);
    if (io->events == 0) {
        if (evport_ctx->nevents == evport_ctx->capacity) {
            evport_ctx_resize(evport_ctx, evport_ctx->capacity * 2);
        }
        ++evport_ctx->nevents;
    }
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    evport_ctx_t* evport_ctx = (evport_ctx_t*)loop->iowatcher;
    if (evport_ctx == NULL) return 0;
    hio_t* io = loop->ios.ptr[fd];

    int evport_events = 0;
    if (io->events & HV_READ) {
        evport_events |= POLLIN;
    }
    if (io->events & HV_WRITE) {
        evport_events |= POLLOUT;
    }
    if (events & HV_READ) {
        evport_events &= ~POLLIN;
    }
    if (events & HV_WRITE) {
        evport_events &= ~POLLOUT;
    }
    if (evport_events == 0) {
        port_dissociate(evport_ctx->port, PORT_SOURCE_FD, fd);
        --evport_ctx->nevents;
    } else {
        port_associate(evport_ctx->port, PORT_SOURCE_FD, fd, evport_events, NULL);
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    evport_ctx_t* evport_ctx = (evport_ctx_t*)loop->iowatcher;
    if (evport_ctx == NULL) return 0;
    struct timespec ts, *tp;
    if (timeout == INFINITE) {
        tp = NULL;
    } else {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        tp = &ts;
    }
    unsigned nevents = 1;
    port_getn(evport_ctx->port, evport_ctx->events, evport_ctx->capacity, &nevents, tp);
    for (int i = 0; i < nevents; ++i) {
        int fd = evport_ctx->events[i].portev_object;
        int revents = evport_ctx->events[i].portev_events;
        hio_t* io = loop->ios.ptr[fd];
        if (io) {
            if (revents & POLLIN) {
                io->revents |= HV_READ;
            }
            if (revents & POLLOUT) {
                io->revents |= HV_WRITE;
            }
            EVENT_PENDING(io);
        }
        // Upon retrieval, the event object is no longer associated with the port.
        iowatcher_add_event(loop, fd, io->events);
    }
    return nevents;
}
#endif
