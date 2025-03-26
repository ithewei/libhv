#ifndef HV_THREAD_H_
#define HV_THREAD_H_

#include "hplatform.h"

#ifdef OS_WIN
#define hv_getpid   (long)GetCurrentProcessId
#else
#define hv_getpid   (long)getpid
#endif

#ifdef OS_WIN
#define hv_gettid   (long)GetCurrentThreadId
#elif HAVE_GETTID || defined(OS_ANDROID)
#define hv_gettid   (long)gettid
#elif defined(OS_LINUX)
#include <sys/syscall.h>
#define hv_gettid() (long)syscall(SYS_gettid)
#elif defined(OS_DARWIN)
static inline long hv_gettid() {
    uint64_t tid = 0;
/* pthread_threadid_np is not available before 10.6 and in 10.6 for ppc */
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060 || defined(__POWERPC__)
    tid = pthread_mach_thread_np(pthread_self());
#else
    pthread_threadid_np(NULL, &tid);
#endif
    return tid;
}
#elif HAVE_PTHREAD_H
#define hv_gettid   (long)pthread_self
#else
#define hv_gettid   hv_getpid
#endif

/*
#include "hthread.h"

HTHREAD_ROUTINE(thread_demo) {
    printf("thread[%ld] start\n", hv_gettid());
    hv_delay(3000);
    printf("thread[%ld] end\n", hv_gettid());
    return 0;
}

int main() {
    hthread_t th = hthread_create(thread_demo, NULL);
    hthread_join(th);
    return 0;
}
 */

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
    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };

    enum SleepPolicy {
        YIELD,
        SLEEP_FOR,
        SLEEP_UNTIL,
        NO_SLEEP,
    };

    HThread() {
        status = STOP;
        status_changed = false;
        dotask_cnt = 0;
        sleep_policy = YIELD;
        sleep_ms = 0;
    }

    virtual ~HThread() {}

    void setStatus(Status stat) {
        status_changed = true;
        status = stat;
    }

    void setSleepPolicy(SleepPolicy policy, uint32_t ms = 0) {
        sleep_policy = policy;
        sleep_ms = ms;
        setStatus(status);
    }

    virtual int start() {
        if (status == STOP) {
            thread = std::thread([this] {
                if (!doPrepare()) return;
                setStatus(RUNNING);
                run();
                setStatus(STOP);
                if (!doFinish()) return;
            });
        }
        return 0;
    }

    virtual int stop() {
        if (status != STOP) {
            setStatus(STOP);
        }
        if (thread.joinable()) {
            thread.join();  // wait thread exit
        }
        return 0;
    }

    virtual int pause() {
        if (status == RUNNING) {
            setStatus(PAUSE);
        }
        return 0;
    }

    virtual int resume() {
        if (status == PAUSE) {
            setStatus(RUNNING);
        }
        return 0;
    }

    virtual void run() {
        while (status != STOP) {
            while (status == PAUSE) {
                std::this_thread::yield();
            }

            doTask();
            ++dotask_cnt;

            HThread::sleep();
        }
    }

    virtual bool doPrepare() {return true;}
    virtual void doTask() {}
    virtual bool doFinish() {return true;}

    std::thread thread;
    std::atomic<Status> status;
    uint32_t dotask_cnt;
protected:
    void sleep() {
        switch (sleep_policy) {
        case YIELD:
            std::this_thread::yield();
            break;
        case SLEEP_FOR:
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            break;
        case SLEEP_UNTIL: {
            if (status_changed) {
                status_changed = false;
                base_tp = std::chrono::steady_clock::now();
            }
            base_tp += std::chrono::milliseconds(sleep_ms);
            std::this_thread::sleep_until(base_tp);
        }
            break;
        default:    // donothing, go all out.
            break;
        }
    }

    SleepPolicy sleep_policy;
    uint32_t    sleep_ms;
    // for SLEEP_UNTIL
    std::atomic<bool> status_changed;
    std::chrono::steady_clock::time_point base_tp;
};
#endif

#endif // HV_THREAD_H_
