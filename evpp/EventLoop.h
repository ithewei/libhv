#ifndef HV_EVENT_LOOP_HPP_
#define HV_EVENT_LOOP_HPP_

#include <functional>
#include <queue>
#include <map>
#include <mutex>

#include "hloop.h"
#include "hthread.h"

#include "Status.h"
#include "Event.h"
#include "ThreadLocalStorage.h"

namespace hv {

class EventLoop : public Status {
public:

    typedef std::function<void()> Functor;

    // New an EventLoop using an existing hloop_t object,
    // so we can embed an EventLoop object into the old application based on hloop.
    // NOTE: Be careful to deal with destroy of hloop_t.
    EventLoop(hloop_t* loop = NULL) {
        setStatus(kInitializing);
        if (loop) {
            loop_ = loop;
            is_loop_owner = false;
        } else {
            loop_ = hloop_new(HLOOP_FLAG_AUTO_FREE);
            is_loop_owner = true;
        }
        setStatus(kInitialized);
    }

    ~EventLoop() {
        stop();
    }

    hloop_t* loop() {
        return loop_;
    }

    // @brief Run loop forever
    void run() {
        if (loop_ == NULL) return;
        if (status() == kRunning) return;
        ThreadLocalStorage::set(ThreadLocalStorage::EVENT_LOOP, this);
        setStatus(kRunning);
        hloop_run(loop_);
        setStatus(kStopped);
    }

    // stop thread-safe
    void stop() {
        if (loop_ == NULL) return;
        if (status() < kRunning) {
            if (is_loop_owner) {
                hloop_free(&loop_);
            }
            loop_ = NULL;
            return;
        }
        setStatus(kStopping);
        hloop_stop(loop_);
        loop_ = NULL;
    }

    void pause() {
        if (loop_ == NULL) return;
        if (isRunning()) {
            hloop_pause(loop_);
            setStatus(kPause);
        }
    }

    void resume() {
        if (loop_ == NULL) return;
        if (isPause()) {
            hloop_resume(loop_);
            setStatus(kRunning);
        }
    }

    // Timer interfaces: setTimer, killTimer, resetTimer
    TimerID setTimer(int timeout_ms, TimerCallback cb, uint32_t repeat = INFINITE, TimerID timerID = INVALID_TIMER_ID) {
        if (loop_ == NULL) return INVALID_TIMER_ID;
        htimer_t* htimer = htimer_add(loop_, onTimer, timeout_ms, repeat);
        if (timerID == INVALID_TIMER_ID) {
            timerID = hevent_id(htimer);
        } else {
            hevent_set_id(htimer, timerID);
        }

        Timer timer(htimer, cb, repeat);
        hevent_set_userdata(htimer, this);

        mutex_.lock();
        timers[timerID] = timer;
        mutex_.unlock();
        return timerID;
    }

    // alias javascript setTimeout, setInterval
    TimerID setTimeout(int timeout_ms, TimerCallback cb) {
        return setTimer(timeout_ms, cb, 1);
    }
    TimerID setInterval(int interval_ms, TimerCallback cb) {
        return setTimer(interval_ms, cb, INFINITE);
    }

    void killTimer(TimerID timerID) {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = timers.find(timerID);
        if (iter != timers.end()) {
            Timer& timer = iter->second;
            htimer_del(timer.timer);
            timers.erase(iter);
        }
    }

    void resetTimer(TimerID timerID) {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = timers.find(timerID);
        if (iter != timers.end()) {
            Timer& timer = iter->second;
            htimer_reset(timer.timer);
            if (timer.repeat == 0) {
                timer.repeat = 1;
            }
        }
    }

    long tid() {
        if (loop_ == NULL) return hv_gettid();
        return hloop_tid(loop_);
    }

    bool isInLoopThread() {
        if (loop_ == NULL) return false;
        return hv_gettid() == hloop_tid(loop_);
    }

    void assertInLoopThread() {
        assert(isInLoopThread());
    }

    void runInLoop(Functor fn) {
        if (isRunning() && isInLoopThread()) {
            if (fn) fn();
        } else {
            queueInLoop(std::move(fn));
        }
    }

    void queueInLoop(Functor fn) {
        postEvent([fn](Event* ev) {
            if (fn) fn();
        });
    }

    void postEvent(EventCallback cb) {
        if (loop_ == NULL) return;

        EventPtr ev(new Event(cb));
        hevent_set_userdata(&ev->event, this);
        ev->event.cb = onCustomEvent;

        mutex_.lock();
        customEvents.push(ev);
        mutex_.unlock();

        hloop_post_event(loop_, &ev->event);
    }

private:
    static void onTimer(htimer_t* htimer) {
        EventLoop* loop = (EventLoop*)hevent_userdata(htimer);

        TimerID timerID = hevent_id(htimer);
        Timer* timer = NULL;

        loop->mutex_.lock();
        auto iter = loop->timers.find(timerID);
        if (iter != loop->timers.end()) {
            timer = &iter->second;
            if (timer->repeat != INFINITE) --timer->repeat;
        }
        loop->mutex_.unlock();

        if (timer) {
            if (timer->cb) timer->cb(timerID);
            if (timer->repeat == 0) {
                // htimer_t alloc and free by hloop, but timers[timerID] managed by EventLoop.
                loop->mutex_.lock();
                loop->timers.erase(timerID);
                loop->mutex_.unlock();
            }
        }
    }

    static void onCustomEvent(hevent_t* hev) {
        EventLoop* loop = (EventLoop*)hevent_userdata(hev);

        loop->mutex_.lock();
        EventPtr ev = loop->customEvents.front();
        loop->customEvents.pop();
        loop->mutex_.unlock();

        if (ev && ev->cb) ev->cb(ev.get());
    }

private:
    hloop_t*                    loop_;
    bool                        is_loop_owner;
    std::mutex                  mutex_;
    std::queue<EventPtr>        customEvents;   // GUAREDE_BY(mutex_)
    std::map<TimerID, Timer>    timers;         // GUAREDE_BY(mutex_)
};

typedef std::shared_ptr<EventLoop> EventLoopPtr;

// ThreadLocalStorage
static inline EventLoop* tlsEventLoop() {
    return (EventLoop*)ThreadLocalStorage::get(ThreadLocalStorage::EVENT_LOOP);
}
#define currentThreadEventLoop tlsEventLoop()

static inline TimerID setTimer(int timeout_ms, TimerCallback cb, uint32_t repeat = INFINITE) {
    EventLoop* loop = tlsEventLoop();
    assert(loop != NULL);
    if (loop == NULL) return INVALID_TIMER_ID;
    return loop->setTimer(timeout_ms, cb, repeat);
}

static inline void killTimer(TimerID timerID) {
    EventLoop* loop = tlsEventLoop();
    assert(loop != NULL);
    if (loop == NULL) return;
    loop->killTimer(timerID);
}

static inline void resetTimer(TimerID timerID) {
    EventLoop* loop = tlsEventLoop();
    assert(loop != NULL);
    if (loop == NULL) return;
    loop->resetTimer(timerID);
}

static inline TimerID setTimeout(int timeout_ms, TimerCallback cb) {
    return setTimer(timeout_ms, cb, 1);
}

static inline TimerID setInterval(int interval_ms, TimerCallback cb) {
    return setTimer(interval_ms, cb, INFINITE);
}

}

#endif // HV_EVENT_LOOP_HPP_
