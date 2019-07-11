#include "io_watcher.h"

#ifdef EVENT_KQUEUE
#include "hplatform.h"
#include "hdef.h"

#define INIT_EVENTS_NUM     64

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
    kqueue_ctx->changes = (struct kevent*)realloc(kqueue_ctx->changes, bytes);
    kqueue_ctx->events = (struct kevent*)realloc(kqueue_ctx->events, bytes);
    kqueue_ctx->capacity = size;
}

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher) return 0;
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)malloc(sizeof(kqueue_ctx_t));
    kqueue_ctx->kqfd = kqueue();
    kqueue_ctx->capacity = INIT_EVENTS_NUM;
    kqueue_ctx->nchanges = 0;
    int bytes = sizeof(struct kevent) * kqueue_ctx->capacity;
    kqueue_ctx->changes = (struct kevent*)malloc(bytes);
    memset(kqueue_ctx->changes, 0, bytes);
    kqueue_ctx->events = (struct kevent*)malloc(bytes);
    memset(kqueue_ctx->events, 0, bytes);
    loop->iowatcher = kqueue_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    close(kqueue_ctx->kqfd);
    SAFE_FREE(kqueue_ctx->changes);
    SAFE_FREE(kqueue_ctx->events);
    SAFE_FREE(loop->iowatcher);
    return 0;
}

static int __add_event(hio_t* io, int event) {
    hloop_t* loop = io->loop;
    if (loop->iowatcher == NULL) {
        hloop_iowatcher_init(loop);
    }
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    int idx = io->event_index[EVENT_INDEX(event)];
    if (idx < 0) {
        io->event_index[EVENT_INDEX(event)] = idx = kqueue_ctx->nchanges;
        kqueue_ctx->nchanges++;
        if (idx == kqueue_ctx->capacity) {
            kqueue_ctx_resize(kqueue_ctx, kqueue_ctx->capacity*2);
        }
        memset(kqueue_ctx->changes+idx, 0, sizeof(struct kevent));
        kqueue_ctx->changes[idx].ident = io->fd;
    }
    assert(kqueue_ctx->changes[idx].ident == io->fd);
    if (events & READ_EVENT) {
        kqueue_ctx->changes[idx].filter = EVFILT_READ;
    }
    else if (events & WRITE_EVENT) {
        kqueue_ctx->changes[idx].filter = EVFILT_WRITE;
    }
    kqueue_ctx->changes[idx].flags = EV_ADD|EV_ENABLE;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    kevent(kqueue_ctx->kqfd, kqueue_ctx->changes, kqueue_ctx->nchanges, NULL, 0, &ts);
    return 0;
}

int iowatcher_add_event(hio_t* io, int events) {
    if (events & READ_EVENT) {
        __add_event(event, READ_EVENT);
    }
    if (events & WRITE_EVENT) {
        __add_event(event, WRITE_EVENT);
    }
    return 0;
}

static int __del_event(hio_t* io, int event) {
    hloop_t* loop = io->loop;
    kqueue_ctx_t* kqueue_ctx = (kqueue_ctx_t*)loop->iowatcher;
    if (kqueue_ctx == NULL) return 0;
    int idx = io->event_index[EVENT_INDEX(event)];
    if (idx < 0) return 0;
    assert(kqueue_ctx->changes[idx].ident == io->fd);
    kqueue_ctx->changes[idx].flags = EV_DELETE;
    io->event_index[EVENT_INDEX(event)] = -1;
    int lastidx = kqueue_ctx->nchanges - 1;
    if (idx < lastidx) {
        // swap
        struct kevent tmp;
        tmp = kqueue_ctx->changes[idx];
        kqueue_ctx->changes[idx] = kqueue_ctx->changes[lastidx];
        kqueue_ctx->changes[lastidx] = tmp;
        auto iter = loop->events.find(kqueue_ctx->changes[idx].ident);
        if (iter != loop->events.end()) {
            iter->second->event_index[kqueue_ctx->changes[idx].filter == EVFILT_READ ? READ_INDEX : WRITE_INDEX] = idx;
        }
    }
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    kevent(kqueue_ctx->kqfd, kqueue_ctx->changes, kqueue_ctx->nchanges, NULL, 0, &ts);
    kqueue_ctx->nchanges--;
    return 0;
}

int iowatcher_del_event(hio_t* io, int events) {
    if (events & READ_EVENT) {
        __del_event(io, READ_EVENT);
    }
    if (events & WRITE_EVENT) {
        __del_event(io, WRITE_EVENT);
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
    int nevent = 0;
    for (int i = 0; i < nkqueue; ++i) {
        if (nevent == nkqueue) break;
        if (kqueue_ctx->events[i].flags & EV_ERROR) {
            continue;
        }
        ++nevent;
        int fd = kqueue_ctx->events[i].ident;
        int revent = kqueue_ctx->events[i].filter;
        hio_t* io = hio_get(loop, fd);
        if (io == NULL) continue;
        io->revents = revent;
        hio_handle_events(io);
    }
    return nevent;
}
#endif
