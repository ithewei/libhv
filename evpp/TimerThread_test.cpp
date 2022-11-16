/*
 * TimerThread_test.cpp
 *
 * @build: make evpp
 *
 */

#include "TimerThread.h"
#include "singleton.h"

namespace hv {

class GlobalTimerThread : public TimerThread {
    SINGLETON_DECL(GlobalTimerThread)
protected:
    GlobalTimerThread() : TimerThread() {}
    ~GlobalTimerThread() {}

public:
    static TimerID setTimeout(int timeout_ms, TimerCallback cb) {
        return GlobalTimerThread::instance()->setTimer(timeout_ms, cb, 1);
    }

    static void clearTimeout(TimerID timerID) {
        GlobalTimerThread::instance()->killTimer(timerID);
    }

    static TimerID setInterval(int interval_ms, TimerCallback cb) {
        return GlobalTimerThread::instance()->setTimer(interval_ms, cb, INFINITE);
    }

    static void clearInterval(TimerID timerID) {
        GlobalTimerThread::instance()->killTimer(timerID);
    }
};

SINGLETON_IMPL(GlobalTimerThread)

} // end namespace hv

int main(int argc, char* argv[]) {
    hv::GlobalTimerThread::setTimeout(3000, [](hv::TimerID timerID) {
        printf("setTimeout timerID=%lu time=%lus\n", (unsigned long)timerID, (unsigned long)time(NULL));
    });

    hv::GlobalTimerThread::setInterval(1000, [](hv::TimerID timerID) {
        printf("setInterval timerID=%lu time=%lus\n", (unsigned long)timerID, (unsigned long)time(NULL));
    });

    // press Enter to stop
    while (getchar() != '\n');

    hv::GlobalTimerThread::exitInstance();
    return 0;
}
