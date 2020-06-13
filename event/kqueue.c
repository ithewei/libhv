#include "iowatcher.h"

#ifdef EVENT_KQUEUE
#include "hplatform.h"
#include "hdef.h"

#include <sys/event.h>

#include "hevent.h"

#define EVENTS_INIT_SIZE     64

#define READ_INDEX  0
#define WRITE_INDEX 1
#define EVENT_INDEX(type) ((type == EVFILT_READ) ? READ_INDEX : WRITE_INDEX)

typedef struct kqueue_ctx_s {
    int kqfd;
    int capacity;
    int nchanges;
    struct kevent* changes;
    //int nevents; // nevents == nchanges
    struct kevent* events;
} kqueue_ctx_t;

static void kqueue_ctx_resize(kqueue_ctx_t* kqueue_ctx, int size) {
    int bytes = sizeof(struct kevent) * size;
    int oldbytes = sizeof(struct kevent) * kqueue_ctx->capacity;
    kqueue_ctx->changes = (struct kevent*)safe_realloc(kqueue_ctx->changes, bytes, oldbytes);
    kqueue_ctx->events = (struct kevent*)safe_realloc(kqueue_ctx->events, bytes, oldbytes);
    kqueue_ctx->capacity = size;
}

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher) return 0;
    kqueue_ctx_t* kqueue_ctx;
    HV_ALLOC_SIZEOF(kqueue_ctx);
    kqueue_ctx->kqfd = kqueue();
    kqueue_ctx->capacity = EVENTS_INIT_SIZE;
    kqueue_ctx->nchanges = 0;
    int bytes = sizeof(struct kevent) * kqueue_ctx->capacity;
    HV_ALLOC(kqueue_ctx->changes, bytes);
    HV_ALLOC(kqueue_ctx->events, bytes);
    loop->iowatcher = kqueue_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    close(kqueue_ctx->kqfd);
    HV_FREE(kqueue_ctx->changes);
    HV_FREE(kqueue_ctx->events);
    HV_FREE(loop->iowatcher);
    return 0;
}

static int __add_event(hloop_t* loop, int fd, int event) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    hio_t* io = loop->ios.ptr[fd];
    int idx = io->event_index[EVENT_INDEX(event)];
    if (idx < 0) {
        io->event_index[EVENT_INDEX(event)] = idx = kqueue_ctx->nchanges;
        kqueue_ctx->nchanges++;
        if (idx == kqueue_ctx->capacity) {
            kqueue_ctx_resize(kqueue_ctx, kqueue_ctx->capacity*2);
        }
        memset(kqueue_ctx->changes+idx, 0, sizeof(struct kevent));
        kqueue_ctx->changes[idx].ident = fd;
    }
    assert(kqueue_ctx->changes[idx].ident == fd);
    kqueue_ctx->changes[idx].filter = event;
    kqueue_ctx->changes[idx].flags = EV_ADD|EV_ENABLE;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    kevent(kqueue_ctx->kqfd, kqueue_ctx->changes, kqueue_ctx->nchanges, NULL, 0, &ts);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (events & HV_READ) {
        __add_event(loop, fd, EVFILT_READ);
    }
    if (events & HV_WRITE) {
        __add_event(loop, fd, EVFILT_WRITE);
    }
    return 0;
}

static int __del_event(hloop_t* loop, int fd, int event) {
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    if (kqueue_ctx == NULL) return 0;
    hio_t* io = loop->ios.ptr[fd];
    int idx = io->event_index[EVENT_INDEX(event)];
    if (idx < 0) return 0;
    assert(kqueue_ctx->changes[idx].ident == fd);
    kqueue_ctx->changes[idx].flags = EV_DELETE;
    io->event_index[EVENT_INDEX(event)] = -1;
    int lastidx = kqueue_ctx->nchanges - 1;
    if (idx < lastidx) {
        // swap
        struct kevent tmp;
        tmp = kqueue_ctx->changes[idx];
        kqueue_ctx->changes[idx] = kqueue_ctx->changes[lastidx];
        kqueue_ctx->changes[lastidx] = tmp;
        hio_t* last = loop->ios.ptr[kqueue_ctx->changes[idx].ident];
        if (last) {
            last->event_index[EVENT_INDEX(kqueue_ctx->changes[idx].filter)] = idx;
        }
    }
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    kevent(kqueue_ctx->kqfd, kqueue_ctx->changes, kqueue_ctx->nchanges, NULL, 0, &ts);
    kqueue_ctx->nchanges--;
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    if (events & HV_READ) {
        __del_event(loop, fd, EVFILT_READ);
    }
    if (events & HV_WRITE) {
        __del_event(loop, fd, EVFILT_WRITE);
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    if (kqueue_ctx == NULL) return 0;
    if (kqueue_ctx->nchanges == 0) return 0;
    struct timespec ts, *tp;
    if (timeout == INFINITE) {
        tp = NULL;
    }
    else {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        tp = &ts;
    }
    int nkqueue = kevent(kqueue_ctx->kqfd, kqueue_ctx->changes, kqueue_ctx->nchanges, kqueue_ctx->events, kqueue_ctx->nchanges, tp);
    if (nkqueue < 0) {
        perror("kevent");
        return nkqueue;
    }
    if (nkqueue == 0) return 0;
    int nevents = 0;
    for (int i = 0; i < nkqueue; ++i) {
        if (kqueue_ctx->events[i].flags & EV_ERROR) {
            continue;
        }
        ++nevents;
        int fd = kqueue_ctx->events[i].ident;
        int revents = kqueue_ctx->events[i].filter;
        hio_t* io = loop->ios.ptr[fd];
        if (io) {
            if (revents & EVFILT_READ) {
                io->revents |= HV_READ;
            }
            if (revents & EVFILT_WRITE) {
                io->revents |= HV_WRITE;
            }
            EVENT_PENDING(io);
        }
        if (nevents == nkqueue) break;
    }
    return nevents;
}
#endif
