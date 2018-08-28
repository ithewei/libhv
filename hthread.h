#ifndef H_THREAD_H
#define H_THREAD_H

#include "hdef.h"
#include "hplatform.h"
#include "htime.h" // for msleep
#include <thread>
#include <atomic>

#ifdef _MSC_VER
inline uint32 getpid(){
    return GetCurrentProcessId();
}
#endif

inline uint32 gettid(){
#ifdef _MSC_VER
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

/************************************************
 * HThread
 * Status: STOP,RUNNING,PAUSE
 * Control: start,stop,pause,resume
 * first-level virtual: doTask
 * second-level virtual: run
************************************************/
class HThread{
public:
    HThread() {
        status = STOP;
    }

    virtual ~HThread() {

    }

    virtual int start() {
        if (status == STOP) {
            status = RUNNING;
            thread = std::thread(&HThread::thread_proc, this);
        }
        return 0;
    }

    virtual int stop() {
        if (status != STOP) {
            status = STOP;
            thread.join(); // wait thread exit
        }
        return 0;
    }

    virtual int pause() {
        if (status == RUNNING) {
            status = PAUSE;
        }
        return 0;
    }

    virtual int resume() {
        if (status == PAUSE) {
            status = RUNNING;
        }
        return 0;
    }

    void thread_proc() {
        doPrepare();
        run();
        doFinish();
    }

    virtual void run() {
        while (status != STOP) {
            if (status == PAUSE) {
                msleep(1);
                continue;
            }

            doTask();

            msleep(1);
        }
    }

    virtual void doPrepare() {}
    virtual void doTask() {}
    virtual void doFinish() {}

    std::thread thread;
    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };
    std::atomic<Status> status;
};

#endif // H_THREAD_H
