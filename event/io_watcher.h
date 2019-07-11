#ifndef IO_WATCHER_H_
#define IO_WATCHER_H_

#include "hloop.h"

int hloop_iowatcher_init(hloop_t* loop);
int hloop_iowatcher_cleanup(hloop_t* loop);
int hloop_handle_ios(hloop_t* loop, int timeout);

#include "hplatform.h"
#if !defined(EVENT_SELECT) && !defined(EVENT_POLL) && !defined(EVENT_EPOLL) && \
    !defined(EVENT_IOCP) && !defined(EVENT_KQUEUE) && !defined(EVENT_NOEVENT)
#ifdef OS_WIN
#define EVENT_IOCP
#elif defined(OS_LINUX)
#define EVENT_EPOLL
#elif defined(OS_MAC)
#define EVENT_KQUEUE
#elif defined(OS_BSD)
#define EVENT_KQUEUE
#else
#define EVENT_SELECT
#endif
#endif

#if defined(EVENT_POLL)
#include <sys/poll.h>
#define READ_EVENT  POLLIN
#define WRITE_EVENT POLLOUT
#elif defined(EVENT_EPOLL)
#include <sys/epoll.h>
#define READ_EVENT  EPOLLIN
#define WRITE_EVENT EPOLLOUT
#elif defined(EVENT_KQUEUE)
#include <sys/event.h>
#define READ_EVENT  EVFILT_READ
#define WRITE_EVENT EVFILT_WRITE
#else
#define READ_EVENT  0x0001
#define WRITE_EVENT 0x0004
#endif

#define ALL_EVENTS  READ_EVENT|WRITE_EVENT

#define READ_INDEX  0
#define WRITE_INDEX 1
#define EVENT_INDEX(type) ((type == READ_EVENT) ? READ_INDEX : WRITE_INDEX)
int iowatcher_init(hloop_t* loop);
int iowatcher_cleanup(hloop_t* loop);
int iowatcher_add_event(hio_t* fd, int events);
int iowatcher_del_event(hio_t* fd, int events);
int iowatcher_poll_events(hloop_t* loop, int timeout);

#endif
