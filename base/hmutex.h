#ifndef HW_MUTEX_H_
#define HW_MUTEX_H_

#include "hplatform.h"

#ifdef _WIN32
#define hmutex_t            CRITICAL_SECTION
#define hmutex_init         InitializeCriticalSection
#define hmutex_destroy      DeleteCriticalSection
#define hmutex_lock         EnterCriticalSection
#define hmutex_unlock       LeaveCriticalSection

#define honce_t             INIT_ONCE
#define HONCE_INIT          INIT_ONCE_STATIC_INIT
typedef void (*honce_fn)(void);
static inline BOOL WINAPI s_once_func(INIT_ONCE* once, PVOID arg, PVOID* _) {
    honce_fn fn = (honce_fn)arg;
    fn();
    return TRUE;
}
static inline void honce(INIT_ONCE* once, honce_fn fn) {
    PVOID dummy = NULL;
    InitOnceExecuteOnce(once, s_once_func, (PVOID)fn, &dummy);
}
#else
#define hmutex_t            pthread_mutex_t
#define hmutex_init(mutex)  pthread_mutex_init(mutex, NULL)
#define hmutex_destroy      pthread_mutex_destroy
#define hmutex_lock         pthread_mutex_lock
#define hmutex_unlock       pthread_mutex_unlock

#define honce_t             pthread_once_t
#define HONCE_INIT          PTHREAD_ONCE_INIT
#define honce               pthread_once
#endif

#ifdef __cplusplus
#include <mutex>
#ifdef _MSC_VER
class RWLock {
 public:
    RWLock() { InitializeSRWLock(&_rwlock); }
    ~RWLock() { }

    void rdlock()   { AcquireSRWLockShared(&_rwlock); }
    void rdunlock() { ReleaseSRWLockShared(&_rwlock); }

    void wrlock()   { AcquireSRWLockExclusive(&_rwlock); }
    void wrunlock() { ReleaseSRWLockExclusive(&_rwlock); }
 private:
    SRWLOCK _rwlock;
};
#else
class RWLock {
 public:
    RWLock() { pthread_rwlock_init(&_rwlock, NULL); }
    ~RWLock() { pthread_rwlock_destroy(&_rwlock); }

    void rdlock()   { pthread_rwlock_rdlock(&_rwlock); }
    void rdunlock() { pthread_rwlock_unlock(&_rwlock); }

    void wrlock()   { pthread_rwlock_wrlock(&_rwlock); }
    void wrunlock() { pthread_rwlock_unlock(&_rwlock); }
 private:
    pthread_rwlock_t _rwlock;
};
#endif
#endif

#endif  // HW_MUTEX_H_
