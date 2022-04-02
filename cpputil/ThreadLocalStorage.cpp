#include "ThreadLocalStorage.h"

#include "hthread.h"

namespace hv {

ThreadLocalStorage ThreadLocalStorage::tls[ThreadLocalStorage::MAX_NUM];

void ThreadLocalStorage::set(int idx, void* val) {
    return tls[idx].set(val);
}

void* ThreadLocalStorage::get(int idx) {
    return tls[idx].get();
}

void ThreadLocalStorage::setThreadName(const char* name) {
    set(THREAD_NAME, (void*)name);
}

const char* ThreadLocalStorage::threadName() {
    void* value = get(THREAD_NAME);
    if (value) {
        return (char*)value;
    }

    static char unnamed[32] = {0};
    snprintf(unnamed, sizeof(unnamed)-1, "thread-%ld", hv_gettid());
    return unnamed;
}

}
