#ifndef HW_THREAD_H_
#define HW_THREAD_H_

#include <thread>
#include <atomic>
#include <chrono>

#include "hplatform.h"
#include "hdef.h"

#ifdef OS_WIN
#define gettid  GetCurrentThreadId
#elif !defined(OS_ANDROID)
#define gettid  pthread_self
#endif

/************************************************
 * HThread
 * Status: STOP,RUNNING,PAUSE
 * Control: start,stop,pause,resume
 * first-level virtual: doTask
 * second-level virtual: run
************************************************/
class HThread {
 public:
    HThread() {
        status = STOP;
        status_switch = false;
        sleep_policy = YIELD;
        sleep_ms = 0;
    }

    virtual ~HThread() {}

    virtual int start() {
        if (status == STOP) {
            switchStatus(RUNNING);
            dotask_cnt = 0;

            thread = std::thread([this]{
                doPrepare();
                run();
                doFinish();
            });
        }
        return 0;
    }

    virtual int stop() {
        if (status != STOP) {
            switchStatus(STOP);
            thread.join();  // wait thread exit
        }
        return 0;
    }

    virtual int pause() {
        if (status == RUNNING) {
            switchStatus(PAUSE);
        }
        return 0;
    }

    virtual int resume() {
        if (status == PAUSE) {
            switchStatus(RUNNING);
        }
        return 0;
    }

    virtual void run() {
        while (status != STOP) {
            while (status == PAUSE) {
                std::this_thread::yield();
            }

            doTask();
            dotask_cnt++;

            sleep();
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
    uint32 dotask_cnt;

    enum SleepPolicy {
        YIELD,
        SLEEP_FOR,
        SLEEP_UNTIL,
        NO_SLEEP,
    };

    void setSleepPolicy(SleepPolicy policy, int64 ms = 0) {
        status_switch = true;
        sleep_policy = policy;
        sleep_ms = ms;
    }

 protected:
    void switchStatus(Status stat) {
        status_switch = true;
        status = stat;
    }

    void sleep() {
        switch (sleep_policy) {
        case YIELD:
            std::this_thread::yield();
            break;
        case SLEEP_FOR:
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            break;
        case SLEEP_UNTIL: {
            if (status_switch) {
                status_switch = false;
                base_tp = std::chrono::system_clock::now();
            }
            base_tp += std::chrono::milliseconds(sleep_ms);
            std::this_thread::sleep_until(base_tp);
        }
            break;
        default:    // donothing, go all out.
            break;
        }
    }

    std::atomic<bool> status_switch;
    SleepPolicy sleep_policy;
    std::chrono::system_clock::time_point base_tp;   // for SLEEP_UNTIL
    int64 sleep_ms;
};

#endif  // HW_THREAD_H_
