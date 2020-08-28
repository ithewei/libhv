#include "hthread.h"
#include "hmutex.h"

#define THREAD_NUM 10
std::mutex g_mutex;

HTHREAD_ROUTINE(test_synchronized) {
    synchronized(g_mutex) {
        hv_delay(1000);
        printf("tid=%ld time=%llus\n", hv_gettid(), (unsigned long long)time(NULL));
    }
    return 0;
}

int main() {
    hthread_t threads[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads[i] = hthread_create(test_synchronized, NULL);
    }
    for (int i = 0; i < THREAD_NUM; ++i) {
        hthread_join(threads[i]);
    }
    return 0;
}
