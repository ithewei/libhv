#ifndef HV_OBJECT_POOL_H_
#define HV_OBJECT_POOL_H_

#include <list>
#include <memory>
#include <mutex>
#include <condition_variable>

#define DEFAULT_OBJECT_POOL_SIZE    4
#define DEFAULT_GET_TIMEOUT         3000 // ms

template<typename T>
class HObjectPool {
public:
    HObjectPool(int size = DEFAULT_OBJECT_POOL_SIZE)
    : pool_size(size), timeout(DEFAULT_GET_TIMEOUT), object_num(0) {
    }

    ~HObjectPool() {
    }

    virtual bool CreateObject(std::shared_ptr<T>& pObj) {
        pObj = std::shared_ptr<T>(new T);
        return true;
    }

    virtual bool InitObject(std::shared_ptr<T>& pObj) {
        return true;
    }

    std::shared_ptr<T> TryGet() {
        std::shared_ptr<T> pObj = NULL;
        std::lock_guard<std::mutex> locker(mutex_);
        if (!objects_.empty()) {
            pObj = objects_.front();
            objects_.pop_front();
        }
        return pObj;
    }

    std::shared_ptr<T> Get() {
        std::shared_ptr<T> pObj = TryGet();
        if (pObj) {
            return pObj;
        }

        std::unique_lock<std::mutex> locker(mutex_);
        if (object_num < pool_size) {
            if (CreateObject(pObj) && InitObject(pObj)) {
                ++object_num;
                return pObj;
            }
        }

        if (timeout > 0) {
            std::cv_status status = cond_.wait_for(locker, std::chrono::milliseconds(timeout));
            if (status == std::cv_status::timeout) {
                return NULL;
            }
            if (!objects_.empty()) {
                pObj = objects_.front();
                objects_.pop_front();
                return pObj;
            }
            else {
                // WARN: objects too little
            }
        }
        return pObj;
    }

    void Release(std::shared_ptr<T>& pObj) {
        objects_.push_back(pObj);
        cond_.notify_one();
    }

    bool Add(std::shared_ptr<T>& pObj) {
        std::lock_guard<std::mutex> locker(mutex_);
        if (object_num >= pool_size) {
            return false;
        }
        objects_.push_back(pObj);
        ++object_num;
        cond_.notify_one();
        return true;
    }

    bool Remove(std::shared_ptr<T>& pObj) {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = objects_.begin();
        while (iter !=  objects_.end()) {
            if (*iter == pObj) {
                iter = objects_.erase(iter);
                --object_num;
                return true;
            }
        }
        return false;
    }

    void RemoveAll() {
        std::lock_guard<std::mutex> locker(mutex_);
        objects_.clear();
    }

    int     pool_size;
    int     timeout;
private:
    int                             object_num;
    std::list<std::shared_ptr<T>>   objects_;
    std::mutex              mutex_;
    std::condition_variable cond_;
};

#endif // HV_OBJECT_POOL_H_
