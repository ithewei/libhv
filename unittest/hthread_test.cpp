#include "hthread.h"
#include "htime.h"

HTHREAD_ROUTINE(test_thread1) {
    int cnt = 10;
    while (cnt-- > 0) {
        printf("tid=%ld time=%llums\n", hv_gettid(), gettimeofday_ms());
        hv_msleep(100);
    }
    return 0;
}

class TestThread2 : public HThread {
protected:
    virtual void run() {
        int cnt = 10;
        while (cnt-- > 0) {
            printf("tid=%ld time=%llums\n", hv_gettid(), gettimeofday_ms());
            hv_msleep(100);
        }
    }
};

class TestThread3 : public HThread {
protected:
    virtual bool doPrepare() {
        printf("doPrepare\n");
        return true;
    }

    virtual void doTask() {
        printf("tid=%ld time=%llums\n", hv_gettid(), gettimeofday_ms());
    }

    virtual bool doFinish() {
        printf("doFinish\n");
        return true;
    }
};

int main() {
    printf("c-style hthread_create\n");
    hthread_t thread1 = hthread_create(test_thread1, NULL);
    hthread_join(thread1);

    printf("cpp-style override HThread::run\n");
    TestThread2 thread2;
    thread2.start();
    thread2.stop();

    printf("cpp-style override HThread::doTask\n");
    TestThread3 thread3;
    thread3.setSleepPolicy(HThread::SLEEP_UNTIL, 100);
    thread3.start();
    hv_sleep(1);
    thread3.stop();

    return 0;
}
