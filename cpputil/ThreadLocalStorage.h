#ifndef HV_THREAD_LOCAL_STORAGE_H_
#define HV_THREAD_LOCAL_STORAGE_H_

#include "hexport.h"
#include "hplatform.h"

#ifdef OS_WIN

#define hthread_key_t               DWORD
#define INVALID_HTHREAD_KEY         0xFFFFFFFF
#define hthread_key_create(pkey)    *pkey = TlsAlloc()
#define hthread_key_delete          TlsFree
#define hthread_get_value           TlsGetValue
#define hthread_set_value           TlsSetValue

#else

#define hthread_key_t               pthread_key_t
#define INVALID_HTHREAD_KEY         0xFFFFFFFF
#define hthread_key_create(pkey)    pthread_key_create(pkey, NULL)
#define hthread_key_delete          pthread_key_delete
#define hthread_get_value           pthread_getspecific
#define hthread_set_value           pthread_setspecific

#endif

#ifdef __cplusplus
namespace hv {

class HV_EXPORT ThreadLocalStorage {
public:
    enum {
        THREAD_NAME = 0,
        EVENT_LOOP  = 1,
        MAX_NUM     = 16,
    };
    ThreadLocalStorage() {
        hthread_key_create(&key);
    }

    ~ThreadLocalStorage() {
        hthread_key_delete(key);
    }

    void set(void* val) {
        hthread_set_value(key, val);
    }

    void* get() {
        return hthread_get_value(key);
    }

    static void  set(int idx, void* val);
    static void* get(int idx);

    static void  setThreadName(const char* name);
    static const char* threadName();

private:
    hthread_key_t key;
    static ThreadLocalStorage tls[MAX_NUM];
};

}
#endif

#endif // HV_THREAD_LOCAL_STORAGE_H_
