#ifndef HV_STATUS_HPP_
#define HV_STATUS_HPP_

#include <atomic>

namespace hv {

class Status {
public:
    enum KStatus {
        kNull           = 0,
        kInitializing   = 1,
        kInitialized    = 2,
        kStarting       = 3,
        kStarted        = 4,
        kRunning        = 5,
        kPause          = 6,
        kStopping       = 7,
        kStopped        = 8,
        kDestroyed      = 9,
    };

    Status() {
        status_ = kNull;
    }
    ~Status() {
        status_ = kDestroyed;
    }

    KStatus status() {
        return status_;
    }

    void setStatus(KStatus status) {
        status_ = status;
    }

    bool isRunning() {
        return status_ == kRunning;
    }

    bool isPause() {
        return status_ == kPause;
    }

    bool isStopped() {
        return status_ == kStopped;
    }

private:
    std::atomic<KStatus> status_;
};

}

#endif // HV_STATUS_HPP_
