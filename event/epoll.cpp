#include "io_watcher.h"

#ifdef EVENT_EPOLL
#include "hio.h"
#include "hplatform.h"
#include "hdef.h"

#define INIT_EVENTS_NUM    64

typedef struct epoll_ctx_s {
    int                 epfd;
    int                 capacity;
    int                 nevents;
    struct epoll_event* events;
} epoll_ctx_t;

static void epoll_ctx_resize(epoll_ctx_t* epoll_ctx, int size) {
    int bytes = sizeof(struct epoll_event) * size;
    epoll_ctx->events = (struct epoll_event*)realloc(epoll_ctx->events, bytes);
    epoll_ctx->capacity = size;
}

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher) return 0;
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)malloc(sizeof(epoll_ctx_t));
    epoll_ctx->epfd = epoll_create(INIT_EVENTS_NUM);
    epoll_ctx->capacity = INIT_EVENTS_NUM;
    epoll_ctx->nevents = 0;
    int bytes = sizeof(struct epoll_event) * epoll_ctx->capacity;
    epoll_ctx->events = (struct epoll_event*)malloc(bytes);
    memset(epoll_ctx->events, 0, bytes);
    loop->iowatcher = epoll_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->iowatcher;
    close(epoll_ctx->epfd);
    SAFE_FREE(epoll_ctx->events);
    SAFE_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hio_t* io, int events) {
    hloop_t* loop = io->loop;
    if (loop->iowatcher == NULL) {
        hloop_iowatcher_init(loop);
    }
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->iowatcher;
    struct epoll_event ee;
    ee.events = 0;
    ee.data.fd = io->fd;
    if (events & READ_EVENT) {
        ee.events |= EPOLLIN;
    }
    if (events & WRITE_EVENT) {
        ee.events |= EPOLLOUT;
    }
    int op = io->events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    epoll_ctl(epoll_ctx->epfd, op, io->fd, &ee);
    if (op == EPOLL_CTL_ADD) {
        if (epoll_ctx->nevents == epoll_ctx->capacity) {
            epoll_ctx_resize(epoll_ctx, epoll_ctx->capacity*2);
        }
        epoll_ctx->nevents++;
    }
    return 0;
}

int iowatcher_del_event(hio_t* io, int events) {
    hloop_t* loop = io->loop;
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->iowatcher;
    if (epoll_ctx == NULL) return 0;

    struct epoll_event ee;
    ee.events = io->events;
    ee.data.fd = io->fd;
    if (events & READ_EVENT) {
        ee.events &= ~EPOLLIN;
    }
    if (events & WRITE_EVENT) {
        ee.events &= ~EPOLLOUT;
    }
    int op = ee.events == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    epoll_ctl(epoll_ctx->epfd, op, io->fd, &ee);
    if (op == EPOLL_CTL_DEL) {
        epoll_ctx->nevents--;
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->iowatcher;
    if (epoll_ctx == NULL)  return 0;
    if (epoll_ctx->nevents == 0) return 0;
    int nepoll = epoll_wait(epoll_ctx->epfd, epoll_ctx->events, epoll_ctx->nevents, timeout);
    if (nepoll < 0) {
        perror("epoll");
        return nepoll;
    }
    if (nepoll == 0) return 0;
    int nevent = 0;
    for (int i = 0; i < epoll_ctx->nevents; ++i) {
        if (nevent == nepoll) break;
        int fd = epoll_ctx->events[i].data.fd;
        uint32_t revents = epoll_ctx->events[i].events;
        if (revents) {
            ++nevent;
            hio_t* io = hio_get(loop, fd);
            if (io == NULL) continue;
            io->revents = revents;
            hio_handle_events(io);
        }
    }
    return nevent;
}
#endif
