#ifndef __HW_EVNET_H_
#define __HW_EVNET_H_

#include "hloop.h"
#include "hplatform.h"

#define READ_EVENT  0x0001
#define WRITE_EVENT 0x0004

#define READ_INDEX  0
#define WRITE_INDEX 1
#define EVENT_INDEX(type) ((type == READ_EVENT) ? READ_INDEX : WRITE_INDEX)

int hloop_event_init(hloop_t* loop);
int hloop_event_cleanup(hloop_t* loop);
int hloop_add_event(hevent_t* event, int type = READ_EVENT|WRITE_EVENT);
int hloop_del_event(hevent_t* event, int type = READ_EVENT|WRITE_EVENT);
int hloop_handle_events(hloop_t* loop, int timeout = INFINITE);

int _on_read(hevent_t* event);
int _on_write(hevent_t* event);

#if !defined(EVENT_SELECT) && !defined(EVENT_POLL) && !defined(EVENT_EPOLL) && \
    !defined(EVENT_IOCP) && !defined(EVENT_KQUEUE) && !defined(EVENT_NOEVENT)
#ifdef OS_WIN
//#define EVENT_IOCP
#define EVENT_SELECT
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
int _event_init(hloop_t* loop);
int _event_cleanup(hloop_t* loop);
int _add_event(hevent_t* event, int type);
int _del_event(hevent_t* event, int type);
int _handle_events(hloop_t* loop, int timeout);

#endif // __HW_EVNET_H_
