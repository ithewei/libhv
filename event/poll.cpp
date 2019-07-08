#include "hevent.h"

#ifdef EVENT_POLL
#include "hplatform.h"
#ifdef OS_LINUX
#include <sys/poll.h>
#endif

#include "hdef.h"

#define INIT_FDS_NUM    64

typedef struct poll_ctx_s {
    int            capacity;
    int            nfds;
    struct pollfd* fds;
} poll_ctx_t;

static void poll_ctx_resize(poll_ctx_t* poll_ctx, int size) {
    int bytes = sizeof(struct pollfd) * size;
    poll_ctx->fds = (struct pollfd*)realloc(poll_ctx->fds, bytes);
    poll_ctx->capacity = size;
}

int _event_init(hloop_t* loop) {
    if (loop->event_ctx)   return 0;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)malloc(sizeof(poll_ctx_t));
    poll_ctx->capacity = INIT_FDS_NUM;
    poll_ctx->nfds = 0;
    int bytes = sizeof(struct pollfd) * poll_ctx->capacity;
    poll_ctx->fds = (struct pollfd*)malloc(bytes);
    memset(poll_ctx->fds, 0, bytes);
    loop->event_ctx = poll_ctx;
    return 0;
}

int _event_cleanup(hloop_t* loop) {
    if (loop->event_ctx == NULL)   return 0;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->event_ctx;
    SAFE_FREE(poll_ctx->fds);
    SAFE_FREE(loop->event_ctx);
    return 0;
}

int _add_event(hevent_t* event, int type) {
    hloop_t* loop = event->loop;
    if (loop->event_ctx == NULL) {
        hloop_event_init(loop);
    }
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->event_ctx;
    int idx = event->event_index[0];
    if (idx < 0) {
        event->event_index[0] = idx = poll_ctx->nfds;
        poll_ctx->nfds++;
        if (idx == poll_ctx->capacity) {
            poll_ctx_resize(poll_ctx, poll_ctx->capacity*2);
        }
        poll_ctx->fds[idx].fd = event->fd;
        poll_ctx->fds[idx].events = 0;
        poll_ctx->fds[idx].revents = 0;
    }
    assert(poll_ctx->fds[idx].fd == event->fd);
    if (type & READ_EVENT) {
        poll_ctx->fds[idx].events |= POLLIN;
    }
    if (type & WRITE_EVENT) {
        poll_ctx->fds[idx].events |= POLLOUT;
    }
    return 0;
}

int _del_event(hevent_t* event, int type) {
    hloop_t* loop = event->loop;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->event_ctx;
    if (poll_ctx == NULL)  return 0;

    int idx = event->event_index[0];
    if (idx < 0) return 0;
    assert(poll_ctx->fds[idx].fd == event->fd);
    if (type & READ_EVENT) {
        poll_ctx->fds[idx].events &= ~POLLIN;
    }
    if (type & WRITE_EVENT) {
        poll_ctx->fds[idx].events &= ~POLLOUT;
    }
    if (poll_ctx->fds[idx].events == 0) {
        event->event_index[0] = -1;
        poll_ctx->nfds--;
        if (idx < poll_ctx->nfds) {
            poll_ctx->fds[idx] = poll_ctx->fds[poll_ctx->nfds];
            auto iter = loop->events.find(poll_ctx->fds[idx].fd);
            if (iter != loop->events.end()) {
                iter->second->event_index[0] = idx;
            }
        }
    }
    return 0;
}

int _handle_events(hloop_t* loop, int timeout) {
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->event_ctx;
    if (poll_ctx == NULL)  return 0;
    if (poll_ctx->nfds == 0)   return 0;
    int npoll = poll(poll_ctx->fds, poll_ctx->nfds, timeout);
    if (npoll < 0) {
        perror("poll");
        return npoll;
    }
    if (npoll == 0) return 0;
    int nevent = 0;
    for (int i = 0; i < poll_ctx->nfds; ++i) {
        if (nevent == npoll) break;
        int fd = poll_ctx->fds[i].fd;
        short revents = poll_ctx->fds[i].revents;
        if (revents) {
            ++nevent;
            auto iter = loop->events.find(fd);
            if (iter == loop->events.end()) {
                continue;
            }
            hevent_t* event = iter->second;
            if (revents & POLLIN) {
                _on_read(event);
            }
            if (revents & POLLOUT) {
                _on_write(event);
            }
        }
    }
    return nevent;
}
#endif
