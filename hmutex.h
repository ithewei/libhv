#ifndef H_MUTEX_H
#define H_MUTEX_H

#include <mutex>

#include "hplatform.h"

class HMutex {
protected:
    std::mutex  _mutex;
};

#ifdef _MSC_VER
class RWLock{
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

#endif // H_MUTEX_H
