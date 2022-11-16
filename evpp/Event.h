#ifndef HV_EVENT_HPP_
#define HV_EVENT_HPP_

#include <functional>
#include <memory>

#include "hloop.h"

namespace hv {

struct Event;
struct Timer;

typedef uint64_t            TimerID;
#define INVALID_TIMER_ID    ((hv::TimerID)-1)

typedef std::function<void(Event*)>     EventCallback;
typedef std::function<void(TimerID)>    TimerCallback;

struct Event {
    hevent_t        event;
    EventCallback   cb;

    Event(EventCallback cb = NULL) {
        memset(&event, 0, sizeof(hevent_t));
        this->cb = std::move(cb);
    }
};

struct Timer {
    htimer_t*       timer;
    TimerCallback   cb;
    uint32_t        repeat;

    Timer(htimer_t* timer = NULL, TimerCallback cb = NULL, uint32_t repeat = INFINITE) {
        this->timer = timer;
        this->cb = std::move(cb);
        this->repeat = repeat;
    }
};

typedef std::shared_ptr<Event> EventPtr;
typedef std::shared_ptr<Timer> TimerPtr;

}

#endif // HV_EVENT_HPP_
