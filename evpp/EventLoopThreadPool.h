#ifndef HV_EVENT_LOOP_THREAD_POOL_HPP_
#define HV_EVENT_LOOP_THREAD_POOL_HPP_

#include <thread>

#include "EventLoopThread.h"

namespace hv {

class EventLoopThreadPool : public Status {
public:
    EventLoopThreadPool(EventLoopPtr master_loop = NULL,
                        int worker_threads = std::thread::hardware_concurrency()) {
        setStatus(kInitializing);
        if (master_loop) {
            master_loop_ = master_loop;
        } else {
            master_loop_.reset(new EventLoop);
        }
        thread_num_ = worker_threads;
        next_loop_idx_ = 0;
        setStatus(kInitialized);
    }

    ~EventLoopThreadPool() {
        stop();
        join();
    }

    int thread_num() {
        return thread_num_;
    }

    EventLoopPtr loop() {
        return master_loop_;
    }

    hloop_t* hloop() {
        return master_loop_->loop();
    }

    EventLoopPtr nextLoop() {
        if (isRunning() && !worker_threads_.empty()) {
            return worker_threads_[++next_loop_idx_ % worker_threads_.size()]->loop();
        } else {
            return master_loop_;
        }
    }

    // @param wait_threads_started: if ture this method will block until all worker_threads started.
    void start(bool wait_threads_started = false) {
        setStatus(kStarting);

        if (thread_num_ == 0) {
            setStatus(kRunning);
            return;
        }

        std::shared_ptr<std::atomic<int>> started_cnt(new std::atomic<int>(0));
        std::shared_ptr<std::atomic<int>> exited_cnt(new std::atomic<int>(0));

        for (int i = 0; i < thread_num_; ++i) {
            auto prefn = [this, started_cnt]() {
                if (++(*started_cnt) == thread_num_) {
                    setStatus(kRunning);
                }
                return 0;
            };
            auto postfn = [this, exited_cnt]() {
                if (++(*exited_cnt) == thread_num_) {
                    setStatus(kStopped);
                }
                return 0;
            };
            EventLoopThreadPtr worker(new EventLoopThread());
            worker->start(false, prefn, postfn);
            worker_threads_.push_back(worker);
        }

        if (wait_threads_started) {
            while (status() < kRunning) {
                hv_delay(1);
            }
        }
    }

    // @param wait_threads_started: if ture this method will block until all worker_threads stopped.
    void stop(bool wait_threads_stopped = false) {
        setStatus(kStopping);

        for (auto& worker : worker_threads_) {
            worker->stop(false);
        }

        if (wait_threads_stopped) {
            while (!isStopped()) {
                hv_delay(1);
            }
        }
    }

    // @brief join all worker_threads
    // @note  destructor will join worker_threads if you forget to call this method.
    void join() {
        for (auto& worker : worker_threads_) {
            worker->join();
        }
    }

private:
    EventLoopPtr                                master_loop_;
    int                                         thread_num_;
    std::vector<EventLoopThreadPtr>             worker_threads_;
    std::atomic<unsigned int>                   next_loop_idx_;
};

}

#endif // HV_EVENT_LOOP_THREAD_POOL_HPP_
