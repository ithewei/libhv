#ifndef HV_TASK_H_
#define HV_TASK_H_

#include <mutex>
#include <condition_variable>

#include "hobj.h"
#include "herr.h"

#define DEFAULT_TASK_TIMEOUT    5000  // ms

class HTask : public HObj {
public:
    enum State {
        TASK_STATE_NOT_READY,
        TASK_STATE_READY,
        TASK_STATE_EXECUTING,
        TASK_STATE_FINISHED,
        TASK_STATE_ERROR
    };

    HTask() {
        state_ = TASK_STATE_NOT_READY;
        errno_ = 0;
        timeout_ = DEFAULT_TASK_TIMEOUT;
    }

    ~HTask() {
    }

    State GetState()    {return state_;}
    int   GetErrno()    {return errno_;}
    void  SetErrno(int errcode) {
        errno_ = errcode;
        if (errno_ != 0) {
            state_ = TASK_STATE_ERROR;
        }
    }

    virtual int Ready() {
        state_ = TASK_STATE_READY;
        return 0;
    }

    virtual int Exec() {
        state_ = TASK_STATE_EXECUTING;
        return 0;
    }

    virtual int Finish() {
        state_ = TASK_STATE_FINISHED;
        wake();
        return 0;
    }

    void SetTimeout(time_t ms) {
        timeout_ = ms;
    }

    int Wait(std::unique_lock<std::mutex>& locker) {
        std::cv_status status = cond_.wait_for(locker, std::chrono::milliseconds(timeout_));
        if (status == std::cv_status::timeout){
            SetErrno(ERR_TASK_TIMEOUT);
            return ERR_TASK_TIMEOUT;
        } else {
            state_ = TASK_STATE_FINISHED;
        }
        return 0;
    }

    void Lock() {
        ctx_mutex.lock();
    }

    void Unlock() {
        ctx_mutex.unlock();
    }

protected:
    void wake() {
        cond_.notify_all();
    }

public:
    std::mutex ctx_mutex;  // provide a ctx mutex
protected:
    State state_;
    int errno_;
    std::condition_variable cond_;
    time_t timeout_;
};

#endif // HV_TASK_H_
