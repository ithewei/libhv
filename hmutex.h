#ifndef HW_MUTEX_H_
#define HW_MUTEX_H_

#include <mutex>

#include "hplatform.h"

#ifdef _WIN32
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
class RWLock{
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

#endif  // HW_MUTEX_H_
