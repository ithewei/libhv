#include "hevent.h"

#ifdef EVENT_SELECT
#include "hplatform.h"
#ifdef OS_LINUX
#include <sys/select.h>
#endif

#include "hdef.h"

typedef struct select_ctx_s {
    int max_fd;
    fd_set readfds;
    fd_set writefds;
    int nread;
    int nwrite;
} select_ctx_t;

int _event_init(hloop_t* loop) {
    if (loop->event_ctx) return 0;
    select_ctx_t* select_ctx = (select_ctx_t*)malloc(sizeof(select_ctx_t));
    select_ctx->max_fd = -1;
    FD_ZERO(&select_ctx->readfds);
    FD_ZERO(&select_ctx->writefds);
    select_ctx->nread = 0;
    select_ctx->nwrite = 0;
    loop->event_ctx = select_ctx;
    return 0;
}

int _event_cleanup(hloop_t* loop) {
    SAFE_FREE(loop->event_ctx);
    return 0;
}

int _add_event(hevent_t* event, int type) {
    hloop_t* loop = event->loop;
    if (loop->event_ctx == NULL) {
        hloop_event_init(loop);
    }
    select_ctx_t* select_ctx = (select_ctx_t*)loop->event_ctx;
    int fd = event->fd;
    if (fd > select_ctx->max_fd) {
        select_ctx->max_fd = fd;
    }
    if (type & READ_EVENT) {
        if (!FD_ISSET(fd, &select_ctx->readfds)) {
            FD_SET(fd, &select_ctx->readfds);
            select_ctx->nread++;
        }
    }
    if (type & WRITE_EVENT) {
        if (!FD_ISSET(fd, &select_ctx->writefds)) {
            FD_SET(fd, &select_ctx->writefds);
            select_ctx->nwrite++;
        }
    }
    return 0;
}

int _del_event(hevent_t* event, int type) {
    hloop_t* loop = event->loop;
    select_ctx_t* select_ctx = (select_ctx_t*)loop->event_ctx;
    if (select_ctx == NULL)    return 0;
    int fd = event->fd;
    if (fd == select_ctx->max_fd) {
        select_ctx->max_fd = -1;
    }
    if (type & READ_EVENT) {
        if (FD_ISSET(fd, &select_ctx->readfds)) {
            FD_CLR(fd, &select_ctx->readfds);
            select_ctx->nread--;
        }
    }
    if (type & WRITE_EVENT) {
        if (FD_ISSET(fd, &select_ctx->writefds)) {
            FD_CLR(fd, &select_ctx->writefds);
            select_ctx->nwrite--;
        }
    }
    return 0;
}

int _handle_events(hloop_t* loop, int timeout) {
    select_ctx_t* select_ctx = (select_ctx_t*)loop->event_ctx;
    if (select_ctx == NULL)    return 0;
    if (select_ctx->nread == 0 && select_ctx->nwrite == 0) {
        return 0;
    }
    int     max_fd = select_ctx->max_fd;
    fd_set  readfds = select_ctx->readfds;
    fd_set  writefds = select_ctx->writefds;
    if (max_fd == -1) {
        for (auto& pair : loop->events) {
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
    auto iter = loop->events.begin();
    while (iter != loop->events.end()) {
        if (nevent == nselect) break;
        int fd = iter->first;
        hevent_t* event = iter->second;
        if (FD_ISSET(fd, &readfds)) {
            ++nevent;
            _on_read(event);
        }
        if (FD_ISSET(fd, &writefds)) {
            ++nevent;
            _on_write(event);
        }
        ++iter;
    }
    return nevent;
}
#endif
