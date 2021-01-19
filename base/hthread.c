#include "hthread.h"

static hthread_key_t tls_thread_name = INVALID_HTHREAD_KEY;

void hthread_setname(const char* name) {
    if (tls_thread_name == INVALID_HTHREAD_KEY) {
        hthread_key_create(&tls_thread_name);
    }
    hthread_set_value(tls_thread_name, name);
}

const char* hthread_getname() {
    static char unnamed[32];
    void* value = hthread_get_value(tls_thread_name);
    if (value) {
        return (char*)value;
    }
    snprintf(unnamed, sizeof(unnamed)-1, "thread-%ld", hv_gettid());
    return unnamed;
}
