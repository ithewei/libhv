#ifndef HV_TASK_QUEUE_H_
#define HV_TASK_QUEUE_H_

#include <memory>
#include <list>
#include <mutex>

#include "htask.h"

class TaskQueue {
public:
    TaskQueue(int size = 0) : max_size(size) {

    }

    std::shared_ptr<HTask> Get() {
        std::lock_guard<std::mutex> locker(mutex_);

        std::shared_ptr<HTask> pTask = NULL;
        if (task_queue_.size() == 0) {
            // task_queue empty
            return pTask;
        }

        pTask = task_queue_.front();
        // note: remove after get
        task_queue_.pop_front();

        return pTask;
    }

    bool Add(std::shared_ptr<HTask> pTask) {
        std::lock_guard<std::mutex> locker(mutex_);

        if (max_size != 0 && task_queue_.size() >= max_size) {
            // task_queue full
            return false;
        }
        task_queue_.push_back(pTask);

        return true;
    }

    bool AddAndWait(std::shared_ptr<HTask> pTask) {
        std::unique_lock<std::mutex> locker(mutex_);

        if (max_size != 0 && task_queue_.size() >= max_size) {
            // task_queue full
            return false;
        }

        task_queue_.push_back(pTask);
        if (pTask->Wait(locker) == ERR_TASK_TIMEOUT) {
            // remove
            for (auto iter = task_queue_.begin(); iter != task_queue_.end(); ++iter) {
                if (*iter == pTask) {
                    task_queue_.erase(iter);
                    return false;
                }
            }
        }

        return true;
    }

    bool Remove(std::shared_ptr<HTask> pTask) {
        std::lock_guard<std::mutex> locker(mutex_);
        for (auto iter = task_queue_.begin(); iter != task_queue_.end(); ++iter) {
            if (*iter == pTask) {
                task_queue_.erase(iter);
                return true;
            }
        }
        return false;
    }

    void RemoveAll() {
        std::lock_guard<std::mutex> locker(mutex_);
        task_queue_.clear();
    }

public:
    size_t max_size;
protected:
    std::list<std::shared_ptr<HTask>>    task_queue_;
    std::mutex                           mutex_;
};

#endif // HV_TASK_QUEUE_H_
