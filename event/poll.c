#include "iowatcher.h"

#ifdef EVENT_POLL
#include "hplatform.h"
#include "hdef.h"
#include "hevent.h"

#ifdef OS_WIN
#define poll        WSAPoll
#endif

#ifdef OS_LINUX
#include <sys/poll.h>
#endif

#include "array.h"
#define FDS_INIT_SIZE   64
ARRAY_DECL(struct pollfd, pollfds);

typedef struct poll_ctx_s {
    int            capacity;
    struct pollfds fds;
} poll_ctx_t;

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher)   return 0;
    poll_ctx_t* poll_ctx;
    HV_ALLOC_SIZEOF(poll_ctx);
    pollfds_init(&poll_ctx->fds, FDS_INIT_SIZE);
    loop->iowatcher = poll_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL)   return 0;
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    pollfds_cleanup(&poll_ctx->fds);
    HV_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    hio_t* io = loop->ios.ptr[fd];
    int idx = io->event_index[0];
    struct pollfd* pfd = NULL;
    if (idx < 0) {
        io->event_index[0] = idx = poll_ctx->fds.size;
        if (idx == poll_ctx->fds.maxsize) {
            pollfds_double_resize(&poll_ctx->fds);
        }
        poll_ctx->fds.size++;
        pfd = poll_ctx->fds.ptr + idx;
        pfd->fd = fd;
        pfd->events = 0;
        pfd->revents = 0;
    }
    else {
        pfd = poll_ctx->fds.ptr + idx;
        assert(pfd->fd == fd);
    }
    if (events & HV_READ) {
        pfd->events |= POLLIN;
    }
    if (events & HV_WRITE) {
        pfd->events |= POLLOUT;
    }
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    if (poll_ctx == NULL)  return 0;
    hio_t* io = loop->ios.ptr[fd];

    int idx = io->event_index[0];
    if (idx < 0) return 0;
    struct pollfd* pfd = poll_ctx->fds.ptr + idx;
    assert(pfd->fd == fd);
    if (events & HV_READ) {
        pfd->events &= ~POLLIN;
    }
    if (events & HV_WRITE) {
        pfd->events &= ~POLLOUT;
    }
    if (pfd->events == 0) {
        pollfds_del_nomove(&poll_ctx->fds, idx);
        // NOTE: correct event_index
        if (idx < poll_ctx->fds.size) {
            hio_t* last = loop->ios.ptr[poll_ctx->fds.ptr[idx].fd];
            last->event_index[0] = idx;
        }
        io->event_index[0] = -1;
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    poll_ctx_t* poll_ctx = (poll_ctx_t*)loop->iowatcher;
    if (poll_ctx == NULL)  return 0;
    if (poll_ctx->fds.size == 0)   return 0;
    int npoll = poll(poll_ctx->fds.ptr, poll_ctx->fds.size, timeout);
    if (npoll < 0) {
        if (errno == EINTR) {
            return 0;
        }
        perror("poll");
        return npoll;
    }
    if (npoll == 0) return 0;
    int nevents = 0;
    for (int i = 0; i < poll_ctx->fds.size; ++i) {
        int fd = poll_ctx->fds.ptr[i].fd;
        short revents = poll_ctx->fds.ptr[i].revents;
        if (revents) {
            ++nevents;
            hio_t* io = loop->ios.ptr[fd];
            if (io) {
                if (revents & (POLLIN | POLLHUP | POLLERR)) {
                    io->revents |= HV_READ;
                }
                if (revents & (POLLOUT | POLLHUP | POLLERR)) {
                    io->revents |= HV_WRITE;
                }
                EVENT_PENDING(io);
            }
        }
        if (nevents == npoll) break;
    }
    return nevents;
}
#endif
