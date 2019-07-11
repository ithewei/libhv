#include "io_watcher.h"

#ifdef EVENT_SELECT
#include "hplatform.h"
#ifdef OS_LINUX
#include <sys/select.h>
#endif

#include "hdef.h"
#include "hio.h"

typedef struct select_ctx_s {
    int max_fd;
    fd_set readfds;
    fd_set writefds;
    int nread;
    int nwrite;
} select_ctx_t;

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher) return 0;
    select_ctx_t* select_ctx = (select_ctx_t*)malloc(sizeof(select_ctx_t));
    select_ctx->max_fd = -1;
    FD_ZERO(&select_ctx->readfds);
    FD_ZERO(&select_ctx->writefds);
    select_ctx->nread = 0;
    select_ctx->nwrite = 0;
    loop->iowatcher = select_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    SAFE_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hio_t* io, int events) {
    hloop_t* loop = io->loop;
    if (loop->iowatcher == NULL) {
        hloop_iowatcher_init(loop);
    }
    select_ctx_t* select_ctx = (select_ctx_t*)loop->iowatcher;
    int fd = io->fd;
    if (fd > select_ctx->max_fd) {
        select_ctx->max_fd = fd;
    }
    if (events & READ_EVENT) {
        if (!FD_ISSET(fd, &select_ctx->readfds)) {
            FD_SET(fd, &select_ctx->readfds);
            select_ctx->nread++;
        }
    }
    if (events & WRITE_EVENT) {
        if (!FD_ISSET(fd, &select_ctx->writefds)) {
            FD_SET(fd, &select_ctx->writefds);
            select_ctx->nwrite++;
        }
    }
    return 0;
}

int iowatcher_del_event(hio_t* io, int events) {
    hloop_t* loop = io->loop;
    select_ctx_t* select_ctx = (select_ctx_t*)loop->iowatcher;
    if (select_ctx == NULL)    return 0;
    int fd = io->fd;
    if (fd == select_ctx->max_fd) {
        select_ctx->max_fd = -1;
    }
    if (events & READ_EVENT) {
        if (FD_ISSET(fd, &select_ctx->readfds)) {
            FD_CLR(fd, &select_ctx->readfds);
            select_ctx->nread--;
        }
    }
    if (events & WRITE_EVENT) {
        if (FD_ISSET(fd, &select_ctx->writefds)) {
            FD_CLR(fd, &select_ctx->writefds);
            select_ctx->nwrite--;
        }
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    select_ctx_t* select_ctx = (select_ctx_t*)loop->iowatcher;
    if (select_ctx == NULL)    return 0;
    if (select_ctx->nread == 0 && select_ctx->nwrite == 0) {
        return 0;
    }
    int     max_fd = select_ctx->max_fd;
    fd_set  readfds = select_ctx->readfds;
    fd_set  writefds = select_ctx->writefds;
    if (max_fd == -1) {
        for (auto& pair : loop->ios) {
            int fd = pair.first;
            if (fd > max_fd) {
                max_fd = fd;
            }
        }
        select_ctx->max_fd = max_fd;
    }
    struct timeval tv, *tp;
    if (timeout == INFINITE) {
        tp = NULL;
    }
    else {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tp = &tv;
    }
    int nselect = select(max_fd+1, &readfds, &writefds, NULL, tp);
    if (nselect < 0) {
#ifdef OS_WIN
        if (WSAGetLastError() == WSAENOTSOCK) {
#else
        if (errno == EBADF) {
            perror("select");
#endif
            return -EBADF;
        }
        return nselect;
    }
    if (nselect == 0)   return 0;
    int nevent = 0;
    auto iter = loop->ios.begin();
    while (iter != loop->ios.end()) {
        if (nevent == nselect) break;
        int fd = iter->first;
        hio_t* io = iter->second;
        if (FD_ISSET(fd, &readfds)) {
            ++nevent;
            io->revents |= READ_EVENT;
        }
        if (FD_ISSET(fd, &writefds)) {
            ++nevent;
            io->revents |= WRITE_EVENT;
        }
        hio_handle_events(io);
        ++iter;
    }
    return nevent;
}
#endif
