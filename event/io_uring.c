#include "iowatcher.h"

#ifdef EVENT_IO_URING
#include "hplatform.h"
#include "hdef.h"
#include "hevent.h"

#include <liburing.h>
#include <poll.h>

#define IO_URING_ENTRIES    1024
#define IO_URING_CANCEL_TAG UINT64_MAX

typedef struct io_uring_ctx_s {
    struct io_uring ring;
    int             nfds;
} io_uring_ctx_t;

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher) return 0;
    io_uring_ctx_t* ctx;
    HV_ALLOC_SIZEOF(ctx);
    int ret = io_uring_queue_init(IO_URING_ENTRIES, &ctx->ring, 0);
    if (ret < 0) {
        HV_FREE(ctx);
        return ret;
    }
    ctx->nfds = 0;
    loop->iowatcher = ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    io_uring_ctx_t* ctx = (io_uring_ctx_t*)loop->iowatcher;
    io_uring_queue_exit(&ctx->ring);
    HV_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    io_uring_ctx_t* ctx = (io_uring_ctx_t*)loop->iowatcher;
    hio_t* io = loop->ios.ptr[fd];

    unsigned poll_mask = 0;
    // pre events
    if (io->events & HV_READ) {
        poll_mask |= POLLIN;
    }
    if (io->events & HV_WRITE) {
        poll_mask |= POLLOUT;
    }
    // now events
    if (events & HV_READ) {
        poll_mask |= POLLIN;
    }
    if (events & HV_WRITE) {
        poll_mask |= POLLOUT;
    }

    struct io_uring_sqe* sqe;
    if (io->events != 0) {
        // Cancel the existing poll request first
        sqe = io_uring_get_sqe(&ctx->ring);
        if (sqe == NULL) return -1;
        io_uring_prep_poll_remove(sqe, (uint64_t)fd);
        io_uring_sqe_set_data64(sqe, IO_URING_CANCEL_TAG);
    } else {
        ctx->nfds++;
    }

    // Add poll for the combined events
    sqe = io_uring_get_sqe(&ctx->ring);
    if (sqe == NULL) return -1;
    io_uring_prep_poll_add(sqe, fd, poll_mask);
    io_uring_sqe_set_data64(sqe, (uint64_t)fd);

    io_uring_submit(&ctx->ring);
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    io_uring_ctx_t* ctx = (io_uring_ctx_t*)loop->iowatcher;
    if (ctx == NULL) return 0;
    hio_t* io = loop->ios.ptr[fd];

    // Calculate remaining events
    unsigned poll_mask = 0;
    // pre events
    if (io->events & HV_READ) {
        poll_mask |= POLLIN;
    }
    if (io->events & HV_WRITE) {
        poll_mask |= POLLOUT;
    }
    // now events
    if (events & HV_READ) {
        poll_mask &= ~POLLIN;
    }
    if (events & HV_WRITE) {
        poll_mask &= ~POLLOUT;
    }

    // Cancel existing poll
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    if (sqe == NULL) return -1;
    io_uring_prep_poll_remove(sqe, (uint64_t)fd);
    io_uring_sqe_set_data64(sqe, IO_URING_CANCEL_TAG);

    if (poll_mask == 0) {
        ctx->nfds--;
    } else {
        // Re-add with remaining events
        sqe = io_uring_get_sqe(&ctx->ring);
        if (sqe == NULL) return -1;
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        io_uring_sqe_set_data64(sqe, (uint64_t)fd);
    }

    io_uring_submit(&ctx->ring);
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    io_uring_ctx_t* ctx = (io_uring_ctx_t*)loop->iowatcher;
    if (ctx == NULL) return 0;
    if (ctx->nfds == 0) return 0;

    struct __kernel_timespec ts;
    struct __kernel_timespec* tp = NULL;
    if (timeout != INFINITE) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000LL;
        tp = &ts;
    }

    struct io_uring_cqe* cqe;
    int ret;
    if (tp) {
        ret = io_uring_wait_cqe_timeout(&ctx->ring, &cqe, tp);
    } else {
        ret = io_uring_wait_cqe(&ctx->ring, &cqe);
    }
    if (ret < 0) {
        if (ret == -ETIME || ret == -EINTR) {
            return 0;
        }
        perror("io_uring_wait_cqe");
        return ret;
    }

    int nevents = 0;
    unsigned head;
    unsigned ncqes = 0;
    io_uring_for_each_cqe(&ctx->ring, head, cqe) {
        ncqes++;
        uint64_t data = io_uring_cqe_get_data64(cqe);
        if (data == IO_URING_CANCEL_TAG) {
            continue;
        }

        int fd = (int)data;
        if (fd < 0 || fd >= loop->ios.maxsize) continue;
        hio_t* io = loop->ios.ptr[fd];
        if (io == NULL) continue;

        if (cqe->res < 0) {
            io->revents |= HV_READ;
            EVENT_PENDING(io);
            ++nevents;
        } else {
            int revents = cqe->res;
            if (revents & (POLLIN | POLLHUP | POLLERR)) {
                io->revents |= HV_READ;
            }
            if (revents & (POLLOUT | POLLHUP | POLLERR)) {
                io->revents |= HV_WRITE;
            }
            if (io->revents) {
                EVENT_PENDING(io);
                ++nevents;
            }
        }

        // io_uring POLL_ADD is one-shot, re-arm for the same events
        unsigned remask = 0;
        if (io->events & HV_READ) remask |= POLLIN;
        if (io->events & HV_WRITE) remask |= POLLOUT;
        if (remask) {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
            if (sqe) {
                io_uring_prep_poll_add(sqe, fd, remask);
                io_uring_sqe_set_data64(sqe, (uint64_t)fd);
            }
        }
    }

    io_uring_cq_advance(&ctx->ring, ncqes);

    if (nevents > 0) {
        io_uring_submit(&ctx->ring);
    }

    return nevents;
}
#endif
