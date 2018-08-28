#ifndef H_SCOPE_H
#define H_SCOPE_H

#include "hdef.h"

template<typename T>
class ScopeFree{
public:
    ScopeFree(T* p) : _p(p) {} 
    ~ScopeFree()    {SAFE_FREE(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeDelete{
public:
    ScopeDelete(T* p) : _p(p) {} 
    ~ScopeDelete()    {SAFE_DELETE(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeDeleteArray{
public:
    ScopeDeleteArray(T* p) : _p(p) {} 
    ~ScopeDeleteArray()    {SAFE_DELETE_ARRAY(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeRelease{
public:
    ScopeRelease(T* p) : _p(p) {} 
    ~ScopeRelease()    {SAFE_RELEASE(_p);}
private:
    T*  _p;
};

template<typename T>
class ScopeLock{
public:
    ScopeLock(T& mutex) : _mutex(mutex) {_mutex.lock();} 
    ~ScopeLock()    {_mutex.unlock();}
private:
    T& _mutex;
};

#endif // H_SCOPE_H