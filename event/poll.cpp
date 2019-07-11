#include "io_watcher.h"

#ifdef EVENT_POLL
#include "hplatform.h"
#include "hdef.h"
#include "hio.h"

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

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher)   return 0;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)malloc(sizeof(poll_ctx_t));
    poll_ctx->capacity = INIT_FDS_NUM;
    poll_ctx->nfds = 0;
    int bytes = sizeof(struct pollfd) * poll_ctx->capacity;
    poll_ctx->fds = (struct pollfd*)malloc(bytes);
    memset(poll_ctx->fds, 0, bytes);
    loop->iowatcher = poll_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL)   return 0;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    SAFE_FREE(poll_ctx->fds);
    SAFE_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hio_t* io, int events) {
    hloop_t* loop = io->loop;
    if (loop->iowatcher == NULL) {
        hloop_iowatcher_init(loop);
    }
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    int idx = io->event_index[0];
    if (idx < 0) {
        io->event_index[0] = idx = poll_ctx->nfds;
        poll_ctx->nfds++;
        if (idx == poll_ctx->capacity) {
            poll_ctx_resize(poll_ctx, poll_ctx->capacity*2);
        }
        poll_ctx->fds[idx].fd = io->fd;
        poll_ctx->fds[idx].events = 0;
        poll_ctx->fds[idx].revents = 0;
    }
    assert(poll_ctx->fds[idx].fd == io->fd);
    if (events & READ_EVENT) {
        poll_ctx->fds[idx].events |= POLLIN;
    }
    if (events & WRITE_EVENT) {
        poll_ctx->fds[idx].events |= POLLOUT;
    }
    return 0;
}

int iowatcher_del_event(hio_t* io, int events) {
    hloop_t* loop = io->loop;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    if (poll_ctx == NULL)  return 0;

    int idx = io->event_index[0];
    if (idx < 0) return 0;
    assert(poll_ctx->fds[idx].fd == io->fd);
    if (events & READ_EVENT) {
        poll_ctx->fds[idx].events &= ~POLLIN;
    }
    if (events & WRITE_EVENT) {
        poll_ctx->fds[idx].events &= ~POLLOUT;
    }
    if (poll_ctx->fds[idx].events == 0) {
        io->event_index[0] = -1;
        poll_ctx->nfds--;
        if (idx < poll_ctx->nfds) {
            poll_ctx->fds[idx] = poll_ctx->fds[poll_ctx->nfds];
            auto iter = loop->ios.find(poll_ctx->fds[idx].fd);
            if (iter != loop->ios.end()) {
                iter->second->event_index[0] = idx;
            }
        }
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
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
            hio_t* io = hio_get(loop, fd);
            if (io == NULL) continue;
            io->revents = revents;
            hio_handle_events(io);
        }
    }
    return nevent;
}
#endif
