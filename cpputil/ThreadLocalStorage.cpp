#include "ThreadLocalStorage.h"

#include "hthread.h"

ThreadLocalStorage ThreadLocalStorage::tls[ThreadLocalStorage::MAX_NUM];

const char* ThreadLocalStorage::threadName() {
    static char unnamed[32] = {0};
    void* value = get(THREAD_NAME);
    if (value) {
        return (char*)value;
    }
    snprintf(unnamed, sizeof(unnamed)-1, "thread-%ld", hv_gettid());
    return unnamed;
}
