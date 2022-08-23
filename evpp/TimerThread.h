#ifndef HV_TIMER_THREAD_HPP_
#define HV_TIMER_THREAD_HPP_

#include "EventLoopThread.h"

namespace hv {

class TimerThread : public EventLoopThread {
public:
    std::atomic<TimerID> nextTimerID;
    TimerThread() : EventLoopThread() {
        nextTimerID = 0;
        start();
    }

    virtual ~TimerThread() {
        stop();
        join();
    }

public:
    // setTimer, setTimeout, killTimer, resetTimer thread-safe
    TimerID setTimer(int timeout_ms, TimerCallback cb, uint32_t repeat = INFINITE) {
        TimerID timerID = ++nextTimerID;
        loop()->setTimerInLoop(timeout_ms, cb, repeat, timerID);
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
        loop()->killTimer(timerID);
    }

    void resetTimer(TimerID timerID, int timeout_ms = 0) {
        loop()->resetTimer(timerID, timeout_ms);
    }
};

} // end namespace hv

#endif // HV_TIMER_THREAD_HPP_
