#include "hevent.h"

#ifdef EVENT_EPOLL
#include "hplatform.h"
#ifdef OS_LINUX
#include <sys/epoll.h>
#endif

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

int _event_init(hloop_t* loop) {
    if (loop->event_ctx) return 0;
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)malloc(sizeof(epoll_ctx_t));
    epoll_ctx->epfd = epoll_create(INIT_EVENTS_NUM);
    epoll_ctx->capacity = INIT_EVENTS_NUM;
    epoll_ctx->nevents = 0;
    int bytes = sizeof(struct epoll_event) * epoll_ctx->capacity;
    epoll_ctx->events = (struct epoll_event*)malloc(bytes);
    memset(epoll_ctx->events, 0, bytes);
    loop->event_ctx = epoll_ctx;
    return 0;
}

int _event_cleanup(hloop_t* loop) {
    if (loop->event_ctx == NULL) return 0;
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->event_ctx;
    close(epoll_ctx->epfd);
    SAFE_FREE(epoll_ctx->events);
    SAFE_FREE(loop->event_ctx);
    return 0;
}

int _add_event(hevent_t* event, int type) {
    hloop_t* loop = event->loop;
    if (loop->event_ctx == NULL) {
        hloop_event_init(loop);
    }
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->event_ctx;
    int op = event->events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    if (type & READ_EVENT) {
        event->events |= EPOLLIN;
    }
    if (type & WRITE_EVENT) {
        event->events |= EPOLLOUT;
    }
    struct epoll_event ee;
    ee.events = event->events;
    ee.data.fd = event->fd;
    epoll_ctl(epoll_ctx->epfd, op, event->fd, &ee);
    if (op == EPOLL_CTL_ADD) {
        if (epoll_ctx->nevents == epoll_ctx->capacity) {
            epoll_ctx_resize(epoll_ctx, epoll_ctx->capacity*2);
        }
        epoll_ctx->nevents++;
    }
    return 0;
}

int _del_event(hevent_t* event, int type) {
    hloop_t* loop = event->loop;
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->event_ctx;
    if (epoll_ctx == NULL) return 0;

    if (event->events == 0) return 0;
    if (type & READ_EVENT) {
        event->events &= ~EPOLLIN;
    }
    if (type & WRITE_EVENT) {
        event->events &= ~EPOLLOUT;
    }
    int op = event->events == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    struct epoll_event ee;
    ee.events = event->events;
    ee.data.fd = event->fd;
    epoll_ctl(epoll_ctx->epfd, op, event->fd, &ee);
    if (op == EPOLL_CTL_DEL) {
        epoll_ctx->nevents--;
    }
    return 0;
}

int _handle_events(hloop_t* loop, int timeout) {
    epoll_ctx_t* epoll_ctx = (epoll_ctx_t*)loop->event_ctx;
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
            auto iter = loop->events.find(fd);
            if (iter == loop->events.end()) {
                continue;
            }
            hevent_t* event = iter->second;
            if (revents & EPOLLIN) {
                _on_read(event);
            }
            if (revents & EPOLLOUT) {
                _on_write(event);
            }
        }
    }
    return nevent;
}
#endif
