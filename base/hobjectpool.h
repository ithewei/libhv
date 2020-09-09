#ifndef HV_OBJECT_POOL_H_
#define HV_OBJECT_POOL_H_

#include <list>
#include <memory>
#include <mutex>
#include <condition_variable>

#define DEFAULT_OBJECT_POOL_INIT_NUM    0
#define DEFAULT_OBJECT_POOL_MAX_NUM     4
#define DEFAULT_OBJECT_POOL_TIMEOUT     3000 // ms

template<class T>
class HObjectFactory {
public:
    static T* create() {
        return new T;
    }
};

template<class T, class TFactory = HObjectFactory<T>>
class HObjectPool {
public:
    HObjectPool(
        int init_num = DEFAULT_OBJECT_POOL_INIT_NUM,
        int max_num = DEFAULT_OBJECT_POOL_MAX_NUM,
        int timeout = DEFAULT_OBJECT_POOL_TIMEOUT)
        : _max_num(max_num)
        , _timeout(timeout)
    {
        for (int i = 0; i < init_num; ++i) {
            T* p = TFactory::create();
            if (p) {
                objects_.push_back(std::shared_ptr<T>(p));
            }
        }
        _object_num = objects_.size();
    }

    ~HObjectPool() {}

    int ObjectNum() { return _object_num; }
    int IdleNum() { return objects_.size(); }
    int BorrowNum() { return ObjectNum() - IdleNum(); }

    std::shared_ptr<T> TryBorrow() {
        std::shared_ptr<T> pObj = NULL;
        std::lock_guard<std::mutex> locker(mutex_);
        if (!objects_.empty()) {
            pObj = objects_.front();
            objects_.pop_front();
        }
        return pObj;
    }

    std::shared_ptr<T> Borrow() {
        std::shared_ptr<T> pObj = TryBorrow();
        if (pObj) {
            return pObj;
        }

        std::unique_lock<std::mutex> locker(mutex_);
        if (_object_num < _max_num) {
            ++_object_num;
            // NOTE: unlock to avoid TFactory::create block
            mutex_.unlock();
            T* p = TFactory::create();
            mutex_.lock();
            if (!p) --_object_num;
            return std::shared_ptr<T>(p);
        }

        if (_timeout > 0) {
            std::cv_status status = cond_.wait_for(locker, std::chrono::milliseconds(_timeout));
            if (status == std::cv_status::timeout) {
                return NULL;
            }
            if (!objects_.empty()) {
                pObj = objects_.front();
                objects_.pop_front();
                return pObj;
            }
            else {
                // WARN: No idle object
            }
        }
        return pObj;
    }

    void Return(std::shared_ptr<T>& pObj) {
        if (!pObj) return;
        std::lock_guard<std::mutex> locker(mutex_);
        objects_.push_back(pObj);
        cond_.notify_one();
    }

    bool Add(std::shared_ptr<T>& pObj) {
        std::lock_guard<std::mutex> locker(mutex_);
        if (_object_num >= _max_num) {
            return false;
        }
        objects_.push_back(pObj);
        ++_object_num;
        cond_.notify_one();
        return true;
    }

    bool Remove(std::shared_ptr<T>& pObj) {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = objects_.begin();
        while (iter !=  objects_.end()) {
            if (*iter == pObj) {
                iter = objects_.erase(iter);
                --_object_num;
                return true;
            }
            else {
                ++iter;
            }
        }
        return false;
    }

    void Clear() {
        std::lock_guard<std::mutex> locker(mutex_);
        objects_.clear();
        _object_num = 0;
    }

    int     _object_num;
    int     _max_num;
    int     _timeout;
private:
    std::list<std::shared_ptr<T>>   objects_;
    std::mutex              mutex_;
    std::condition_variable cond_;
};

template<class T, class TFactory = HObjectFactory<T>>
class HPoolObject {
public:
    typedef HObjectPool<T, TFactory> PoolType;

    HPoolObject(PoolType& pool) : pool_(pool)
    {
        sptr_ = pool_.Borrow();
    }

    ~HPoolObject() {
        if (sptr_) {
            pool_.Return(sptr_);
        }
    }

    HPoolObject(const HPoolObject<T>&) = delete;
    HPoolObject<T>& operator=(const HPoolObject<T>&) = delete;

    T* get() {
        return sptr_.get();
    }

    operator bool() {
        return sptr_.get() != NULL;
    }

    T* operator->() {
        return sptr_.get();
    }

    T operator*() {
        return *sptr_.get();
    }

private:
    PoolType& pool_;
    std::shared_ptr<T> sptr_;
};

#endif // HV_OBJECT_POOL_H_
