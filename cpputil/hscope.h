#ifndef HV_SCOPE_H_
#define HV_SCOPE_H_

#include <functional>
typedef std::function<void()> Function;

#include "hdef.h"

// same as golang defer
class Defer {
public:
    Defer(Function&& fn) : _fn(std::move(fn)) {}
    ~Defer() { if(_fn) _fn();}
private:
    Function _fn;
};
#define defer(code) Defer STRINGCAT(_defer_, __LINE__)([&](){code});

class ScopeCleanup {
public:
    template<typename Fn, typename... Args>
    ScopeCleanup(Fn&& fn, Args&&... args) {
        _cleanup = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

    ~ScopeCleanup() {
        _cleanup();
    }

private:
    Function _cleanup;
};

template<typename T>
class ScopeFree {
public:
    ScopeFree(T* p) : _p(p) {}
    ~ScopeFree()    {SAFE_FREE(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeDelete {
public:
    ScopeDelete(T* p) : _p(p) {}
    ~ScopeDelete()    {SAFE_DELETE(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeDeleteArray {
public:
    ScopeDeleteArray(T* p) : _p(p) {}
    ~ScopeDeleteArray()    {SAFE_DELETE_ARRAY(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeRelease {
public:
    ScopeRelease(T* p) : _p(p) {}
    ~ScopeRelease()    {SAFE_RELEASE(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeLock {
public:
    ScopeLock(T& mutex) : _mutex(mutex) {_mutex.lock();}
    ~ScopeLock()    {_mutex.unlock();}
private:
    T& _mutex;
};

#endif // HV_SCOPE_H_
