#ifndef HV_MUTEX_H_
#define HV_MUTEX_H_

#include "hexport.h"
#include "hplatform.h"
#include "htime.h"

BEGIN_EXTERN_C

#ifdef OS_WIN
#define hmutex_t                CRITICAL_SECTION
#define hmutex_init             InitializeCriticalSection
#define hmutex_destroy          DeleteCriticalSection
#define hmutex_lock             EnterCriticalSection
#define hmutex_trylock          TryEnterCriticalSection
#define hmutex_unlock           LeaveCriticalSection

#define hrecursive_mutex_t          CRITICAL_SECTION
#define hrecursive_mutex_init       InitializeCriticalSection
#define hrecursive_mutex_destroy    DeleteCriticalSection
#define hrecursive_mutex_lock       EnterCriticalSection
#define hrecursive_mutex_unlock     LeaveCriticalSection

#define HSPINLOCK_COUNT         -1
#define hspinlock_t             CRITICAL_SECTION
#define hspinlock_init(pspin)   InitializeCriticalSectionAndSpinCount(pspin, HSPINLOCK_COUNT)
#define hspinlock_destroy       DeleteCriticalSection
#define hspinlock_lock          EnterCriticalSection
#define hspinlock_unlock        LeaveCriticalSection

#define hrwlock_t               SRWLOCK
#define hrwlock_init            InitializeSRWLock
#define hrwlock_destroy(plock)
#define hrwlock_rdlock          AcquireSRWLockShared
#define hrwlock_rdunlock        ReleaseSRWLockShared
#define hrwlock_wrlock          AcquireSRWLockExclusive
#define hrwlock_wrunlock        ReleaseSRWLockExclusive

#define htimed_mutex_t                  HANDLE
#define htimed_mutex_init(pmutex)       *(pmutex) = CreateMutex(NULL, FALSE, NULL)
#define htimed_mutex_destroy(pmutex)    CloseHandle(*(pmutex))
#define htimed_mutex_lock(pmutex)       WaitForSingleObject(*(pmutex), INFINITE)
#define htimed_mutex_unlock(pmutex)     ReleaseMutex(*(pmutex))
// true:  WAIT_OBJECT_0
// false: WAIT_OBJECT_TIMEOUT
#define htimed_mutex_lock_for(pmutex, ms)   ( WaitForSingleObject(*(pmutex), ms) == WAIT_OBJECT_0 )

#define hcondvar_t                      CONDITION_VARIABLE
#define hcondvar_init                   InitializeConditionVariable
#define hcondvar_destroy(pcond)
#define hcondvar_wait(pcond, pmutex)            SleepConditionVariableCS(pcond, pmutex, INFINITE)
#define hcondvar_wait_for(pcond, pmutex, ms)    SleepConditionVariableCS(pcond, pmutex, ms)
#define hcondvar_signal                 WakeConditionVariable
#define hcondvar_broadcast              WakeAllConditionVariable

#define honce_t                 INIT_ONCE
#define HONCE_INIT              INIT_ONCE_STATIC_INIT
typedef void (*honce_fn)();
static inline BOOL WINAPI s_once_func(INIT_ONCE* once, PVOID arg, PVOID* _) {
    honce_fn fn = (honce_fn)arg;
    fn();
    return TRUE;
}
static inline void honce(honce_t* once, honce_fn fn) {
    PVOID dummy = NULL;
    InitOnceExecuteOnce(once, s_once_func, (PVOID)fn, &dummy);
}

#define hsem_t                      HANDLE
#define hsem_init(psem, value)      *(psem) = CreateSemaphore(NULL, value, value+100000, NULL)
#define hsem_destroy(psem)          CloseHandle(*(psem))
#define hsem_wait(psem)             WaitForSingleObject(*(psem), INFINITE)
#define hsem_post(psem)             ReleaseSemaphore(*(psem), 1, NULL)
// true:  WAIT_OBJECT_0
// false: WAIT_OBJECT_TIMEOUT
#define hsem_wait_for(psem, ms)     ( WaitForSingleObject(*(psem), ms) == WAIT_OBJECT_0 )

#else
#define hmutex_t                pthread_mutex_t
#define hmutex_init(pmutex)     pthread_mutex_init(pmutex, NULL)
#define hmutex_destroy          pthread_mutex_destroy
#define hmutex_lock             pthread_mutex_lock
#define hmutex_trylock(pmutex)  (pthread_mutex_trylock(pmutex) == 0)
#define hmutex_unlock           pthread_mutex_unlock

#define hrecursive_mutex_t          pthread_mutex_t
#define hrecursive_mutex_init(pmutex) \
    do {\
        pthread_mutexattr_t attr;\
        pthread_mutexattr_init(&attr);\
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);\
        pthread_mutex_init(pmutex, &attr);\
    } while(0)
#define hrecursive_mutex_destroy    pthread_mutex_destroy
#define hrecursive_mutex_lock       pthread_mutex_lock
#define hrecursive_mutex_unlock     pthread_mutex_unlock

#if HAVE_PTHREAD_SPIN_LOCK
#define hspinlock_t             pthread_spinlock_t
#define hspinlock_init(pspin)   pthread_spin_init(pspin, PTHREAD_PROCESS_PRIVATE)
#define hspinlock_destroy       pthread_spin_destroy
#define hspinlock_lock          pthread_spin_lock
#define hspinlock_unlock        pthread_spin_unlock
#else
#define hspinlock_t             pthread_mutex_t
#define hspinlock_init(pmutex)  pthread_mutex_init(pmutex, NULL)
#define hspinlock_destroy       pthread_mutex_destroy
#define hspinlock_lock          pthread_mutex_lock
#define hspinlock_unlock        pthread_mutex_unlock
#endif

#define hrwlock_t               pthread_rwlock_t
#define hrwlock_init(prwlock)   pthread_rwlock_init(prwlock, NULL)
#define hrwlock_destroy         pthread_rwlock_destroy
#define hrwlock_rdlock          pthread_rwlock_rdlock
#define hrwlock_rdunlock        pthread_rwlock_unlock
#define hrwlock_wrlock          pthread_rwlock_wrlock
#define hrwlock_wrunlock        pthread_rwlock_unlock

#define htimed_mutex_t              pthread_mutex_t
#define htimed_mutex_init(pmutex)   pthread_mutex_init(pmutex, NULL)
#define htimed_mutex_destroy        pthread_mutex_destroy
#define htimed_mutex_lock           pthread_mutex_lock
#define htimed_mutex_unlock         pthread_mutex_unlock
static inline void timespec_after(struct timespec* ts, unsigned int ms) {
    struct timeval  tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + ms / 1000;
    ts->tv_nsec = tv.tv_usec * 1000 + ms % 1000 * 1000000;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_nsec -= 1000000000;
        ts->tv_sec += 1;
    }
}
// true:  OK
// false: ETIMEDOUT
static inline int htimed_mutex_lock_for(htimed_mutex_t* mutex, unsigned int ms) {
#if HAVE_PTHREAD_MUTEX_TIMEDLOCK
    struct timespec ts;
    timespec_after(&ts, ms);
    return pthread_mutex_timedlock(mutex, &ts) != ETIMEDOUT;
#else
    int ret = 0;
    unsigned int end = gettick_ms() + ms;
    while ((ret = pthread_mutex_trylock(mutex)) != 0) {
        if (gettick_ms() >= end) {
            break;
        }
        hv_msleep(1);
    }
    return ret == 0;
#endif
}

#define hcondvar_t              pthread_cond_t
#define hcondvar_init(pcond)    pthread_cond_init(pcond, NULL)
#define hcondvar_destroy        pthread_cond_destroy
#define hcondvar_wait           pthread_cond_wait
#define hcondvar_signal         pthread_cond_signal
#define hcondvar_broadcast      pthread_cond_broadcast
// true:  OK
// false: ETIMEDOUT
static inline int hcondvar_wait_for(hcondvar_t* cond, hmutex_t* mutex, unsigned int ms) {
    struct timespec ts;
    timespec_after(&ts, ms);
    return pthread_cond_timedwait(cond, mutex, &ts) != ETIMEDOUT;
}

#define honce_t                 pthread_once_t
#define HONCE_INIT              PTHREAD_ONCE_INIT
#define honce                   pthread_once

#include <semaphore.h>
#define hsem_t                  sem_t
#define hsem_init(psem, value)  sem_init(psem, 0, value)
#define hsem_destroy            sem_destroy
#define hsem_wait               sem_wait
#define hsem_post               sem_post
// true:  OK
// false: ETIMEDOUT
static inline int hsem_wait_for(hsem_t* sem, unsigned int ms) {
#if HAVE_SEM_TIMEDWAIT
    struct timespec ts;
    timespec_after(&ts, ms);
    return sem_timedwait(sem, &ts) != ETIMEDOUT;
#else
    int ret = 0;
    unsigned int end = gettick_ms() + ms;
    while ((ret = sem_trywait(sem)) != 0) {
        if (gettick_ms() >= end) {
            break;
        }
        hv_msleep(1);
    }
    return ret == 0;
#endif
}

#endif

END_EXTERN_C

#ifdef __cplusplus
#include <mutex>
#include <condition_variable>
// using std::mutex;
// NOTE: test std::timed_mutex incorrect in some platforms, use htimed_mutex_t
// using std::timed_mutex;
using std::condition_variable;
using std::lock_guard;
using std::unique_lock;

BEGIN_NAMESPACE_HV

class MutexLock {
public:
    MutexLock() { hmutex_init(&_mutex); }
    ~MutexLock() { hmutex_destroy(&_mutex); }

    void lock() { hmutex_lock(&_mutex); }
    void unlock() { hmutex_unlock(&_mutex); }
protected:
    hmutex_t _mutex;
};

class SpinLock {
public:
    SpinLock() { hspinlock_init(&_spin); }
    ~SpinLock() { hspinlock_destroy(&_spin); }

    void lock() { hspinlock_lock(&_spin); }
    void unlock() { hspinlock_unlock(&_spin); }
protected:
    hspinlock_t _spin;
};

class RWLock {
public:
    RWLock()    { hrwlock_init(&_rwlock); }
    ~RWLock()   { hrwlock_destroy(&_rwlock); }

    void rdlock()   { hrwlock_rdlock(&_rwlock); }
    void rdunlock() { hrwlock_rdunlock(&_rwlock); }

    void wrlock()   { hrwlock_wrlock(&_rwlock); }
    void wrunlock() { hrwlock_wrunlock(&_rwlock); }

    void lock()     { rdlock(); }
    void unlock()   { rdunlock(); }
protected:
    hrwlock_t   _rwlock;
};

template<class T>
class LockGuard {
public:
    LockGuard(T& t) : _lock(t) { _lock.lock(); }
    ~LockGuard() { _lock.unlock(); }
protected:
    T& _lock;
};

END_NAMESPACE_HV

// same as java synchronized(lock) { ... }
#define synchronized(lock) for (std::lock_guard<std::mutex> _lock_(lock), *p = &_lock_; p != NULL; p = NULL)

#endif // __cplusplus

#endif // HV_MUTEX_H_
