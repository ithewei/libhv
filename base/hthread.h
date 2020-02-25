#ifndef HV_THREAD_H_
#define HV_THREAD_H_

#include "hplatform.h"
#include "hdef.h"

#ifdef OS_WIN
#define gettid  GetCurrentThreadId
#elif HAVE_GETTID
#elif defined(OS_ANDROID)
#elif defined(OS_LINUX)
#include <sys/syscall.h>
static inline int gettid() {
    return syscall(SYS_gettid);
}
#elif HAVE_PTHREAD_H
#define gettid  pthread_self
#endif

#ifdef OS_WIN
typedef HANDLE      hthread_t;
typedef DWORD (WINAPI *hthread_routine)(void*);
#define HTHREAD_RETTYPE DWORD
#define HTHREAD_ROUTINE(fname) DWORD WINAPI fname(void* userdata)
static inline hthread_t hthread_create(hthread_routine fn, void* userdata) {
    return CreateThread(NULL, 0, fn, userdata, 0, NULL);
}

static inline int hthread_join(hthread_t th) {
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    return 0;
}
#else
typedef pthread_t   hthread_t;
typedef void* (*hthread_routine)(void*);
#define HTHREAD_RETTYPE void*
#define HTHREAD_ROUTINE(fname) void* fname(void* userdata)
static inline hthread_t hthread_create(hthread_routine fn, void* userdata) {
    pthread_t th;
    pthread_create(&th, NULL, fn, userdata);
    return th;
}

static inline int hthread_join(hthread_t th) {
    return pthread_join(th, NULL);
}
#endif

#ifdef __cplusplus
/************************************************
 * HThread
 * Status: STOP,RUNNING,PAUSE
 * Control: start,stop,pause,resume
 * first-level virtual: doTask
 * second-level virtual: run
************************************************/
#include <thread>
#include <atomic>
#include <chrono>

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

            thread = std::thread([this] {
                if (!doPrepare()) return;
                run();
                if (!doFinish()) return;
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

    virtual bool doPrepare() {return true;}
    virtual void doTask() {}
    virtual bool doFinish() {return true;}

    std::thread thread;
    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };
    std::atomic<Status> status;
    uint32_t dotask_cnt;

    enum SleepPolicy {
        YIELD,
        SLEEP_FOR,
        SLEEP_UNTIL,
        NO_SLEEP,
    };

    void setSleepPolicy(SleepPolicy policy, uint32_t ms = 0) {
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
    uint32_t sleep_ms;
};
#endif

#endif // HV_THREAD_H_
