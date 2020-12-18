#ifndef HV_EVENT_LOOP_HPP_
#define HV_EVENT_LOOP_HPP_

#include <functional>
#include <queue>
#include <map>
#include <mutex>

#include "hloop.h"
#include "hthread.h"

#include "Event.h"

namespace hv {

class EventLoop {
public:
    typedef std::function<void()> Functor;

    EventLoop() {
        loop_ = hloop_new(HLOOP_FLAG_AUTO_FREE);
        assert(loop_ != NULL);
        hloop_set_userdata(loop_, this);
    }

    ~EventLoop() {
        stop();
    }

    void start() {
        if (loop_ == NULL) return;
        hloop_run(loop_);
    }

    void stop() {
        if (loop_ == NULL) return;
        hloop_stop(loop_);
        loop_ = NULL;
    }

    void pause() {
        if (loop_ == NULL) return;
        hloop_pause(loop_);
    }

    void resume() {
        if (loop_ == NULL) return;
        hloop_resume(loop_);
    }

    TimerID setTimer(int timeout_ms, TimerCallback cb, int repeat = INFINITE) {
        htimer_t* htimer = htimer_add(loop_, onTimer, timeout_ms, repeat);

        Timer timer(htimer, cb, repeat);
        hevent_set_userdata(htimer, &timer);

        TimerID timerID = hevent_id(htimer);

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

    bool isInLoop() {
        return hv_gettid() == hloop_tid(loop_);
    }

    void assertInLoop() {
        assert(isInLoop());
    }

    void runInLoop(Functor fn) {
        if (isInLoop()) {
            if (fn) fn();
        } else {
            queueInLoop(fn);
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
        ev->event.cb = onCustomEvent;

        mutex_.lock();
        customEvents.push(ev);
        mutex_.unlock();

        hloop_post_event(loop_, &ev->event);
    }

private:
    static void onTimer(htimer_t* htimer) {
        hloop_t* hloop = (hloop_t*)hevent_loop(htimer);
        EventLoop* loop = (EventLoop*)hloop_userdata(hloop);

        TimerID timerID = hevent_id(htimer);
        TimerCallback cb = NULL;

        loop->mutex_.lock();
        auto iter = loop->timers.find(timerID);
        if (iter != loop->timers.end()) {
            Timer& timer = iter->second;
            cb = timer.cb;
            --timer.repeat;
        }
        loop->mutex_.unlock();

        if (cb) cb(timerID);

        // NOTE: refind iterator, because iterator may be invalid
        // if the timer-related interface is called in the callback function above.
        loop->mutex_.lock();
        iter = loop->timers.find(timerID);
        if (iter != loop->timers.end()) {
            Timer& timer = iter->second;
            if (timer.repeat == 0) {
                // htimer_t alloc and free by hloop, but timers[timerID] managed by EventLoop.
                loop->timers.erase(iter);
            }
        }
        loop->mutex_.unlock();
    }

    static void onCustomEvent(hevent_t* hev) {
        hloop_t* hloop = (hloop_t*)hevent_loop(hev);
        EventLoop* loop = (EventLoop*)hloop_userdata(hloop);

        loop->mutex_.lock();
        EventPtr ev = loop->customEvents.front();
        loop->customEvents.pop();
        loop->mutex_.unlock();

        if (ev && ev->cb) ev->cb(ev.get());
    }

private:
    hloop_t*                    loop_;
    std::mutex                  mutex_;
    std::queue<EventPtr>        customEvents;   // GUAREDE_BY(mutex_)
    std::map<TimerID, Timer>    timers;         // GUAREDE_BY(mutex_)
};

}

#endif // HV_EVENT_LOOP_HPP_
