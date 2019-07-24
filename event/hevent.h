#ifndef HW_EVENT_H_
#define HW_EVENT_H_

#include "hloop.h"

#include "hdef.h"

#define EVENT_ENTRY(p)          container_of(p, hevent_t, pending_node)
#define IDLE_ENTRY(p)           container_of(p, hidle_t,  node)
#define TIMER_ENTRY(p)          container_of(p, htimer_t, node)

#define EVENT_ACTIVE(ev) \
    if (!ev->active) {\
        ev->active = 1;\
        ev->loop->nactives++;\
    }\

#define EVENT_INACTIVE(ev) \
    if (ev->active) {\
        ev->active = 0;\
        ev->loop->nactives--;\
    }\

#define EVENT_PENDING(ev) \
    do {\
        if (!ev->pending) {\
            ev->pending = 1;\
            ev->loop->npendings++;\
            hevent_t** phead = &ev->loop->pendings[HEVENT_PRIORITY_INDEX(ev->priority)];\
            if (*phead == NULL) {\
                *phead = (hevent_t*)ev;\
            } else {\
                ev->pending_next = *phead;\
                *phead = (hevent_t*)ev;\
            }\
        }\
    } while(0)

#define EVENT_ADD(loop, ev, cb) \
    do {\
        ev->loop = loop;\
        ev->event_id = ++loop->event_counter;\
        ev->cb = (hevent_cb)cb;\
        EVENT_ACTIVE(ev);\
    } while(0)

#define EVENT_DEL(ev) \
    do {\
        EVENT_INACTIVE(ev);\
        ev->destroy = 1;\
    } while(0)

#endif // HW_EVENT_H_
