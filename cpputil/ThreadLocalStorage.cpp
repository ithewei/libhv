#include "ThreadLocalStorage.h"

#include "hthread.h"

namespace hv {

static inline bool tls_index_valid(int idx) {
    return idx >= 0 && idx < ThreadLocalStorage::MAX_NUM;
}

ThreadLocalStorage ThreadLocalStorage::tls[ThreadLocalStorage::MAX_NUM];

void ThreadLocalStorage::set(int idx, void* val) {
    if (!tls_index_valid(idx)) return;
    tls[idx].set(val);
}

void* ThreadLocalStorage::get(int idx) {
    if (!tls_index_valid(idx)) return NULL;
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

    thread_local char unnamed[32] = {0};
    snprintf(unnamed, sizeof(unnamed) - 1, "thread-%ld", hv_gettid());
    return unnamed;
}

}
