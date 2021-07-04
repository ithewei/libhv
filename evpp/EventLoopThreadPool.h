#ifndef HV_EVENT_LOOP_THREAD_POOL_HPP_
#define HV_EVENT_LOOP_THREAD_POOL_HPP_

#include "EventLoopThread.h"

namespace hv {

class EventLoopThreadPool : public Status {
public:
    EventLoopThreadPool(int thread_num = std::thread::hardware_concurrency()) {
        setStatus(kInitializing);
        thread_num_ = thread_num;
        next_loop_idx_ = 0;
        setStatus(kInitialized);
    }

    ~EventLoopThreadPool() {
        stop();
        join();
    }

    int threadNum() {
        return thread_num_;
    }

    void setThreadNum(int num) {
        thread_num_ = num;
    }

    EventLoopPtr nextLoop() {
        if (loop_threads_.empty()) return NULL;
        return loop_threads_[++next_loop_idx_ % loop_threads_.size()]->loop();
    }

    EventLoopPtr loop(int idx = -1) {
        if (idx >= 0 && idx < loop_threads_.size()) {
            return loop_threads_[idx]->loop();
        }
        return nextLoop();
    }

    hloop_t* hloop(int idx = -1) {
        EventLoopPtr ptr = loop(idx);
        return ptr ? ptr->loop() : NULL;
    }

    // @param wait_threads_started: if ture this method will block until all loop_threads started.
    // @param pre: This functor will be executed when loop_thread started.
    // @param post:This Functor will be executed when loop_thread stopped.
    void start(bool wait_threads_started = false,
               std::function<void(const EventLoopPtr&)> pre = NULL,
               std::function<void(const EventLoopPtr&)> post = NULL) {
        setStatus(kStarting);

        if (thread_num_ == 0) {
            setStatus(kRunning);
            return;
        }

        std::shared_ptr<std::atomic<int>> started_cnt(new std::atomic<int>(0));
        std::shared_ptr<std::atomic<int>> exited_cnt(new std::atomic<int>(0));

        for (int i = 0; i < thread_num_; ++i) {
            EventLoopThreadPtr loop_thread(new EventLoopThread);
            const EventLoopPtr& loop = loop_thread->loop();
            loop_thread->start(false,
                [this, started_cnt, pre, &loop]() {
                    if (++(*started_cnt) == thread_num_) {
                        setStatus(kRunning);
                    }
                    if (pre) pre(loop);
                    return 0;
                },
                [this, exited_cnt, post, &loop]() {
                    if (post) post(loop);
                    if (++(*exited_cnt) == thread_num_) {
                        setStatus(kStopped);
                    }
                    return 0;
                }
            );
            loop_threads_.push_back(loop_thread);
        }

        if (wait_threads_started) {
            while (status() < kRunning) {
                hv_delay(1);
            }
        }
    }

    // @param wait_threads_started: if ture this method will block until all loop_threads stopped.
    void stop(bool wait_threads_stopped = false) {
        setStatus(kStopping);

        for (auto& loop_thread : loop_threads_) {
            loop_thread->stop(false);
        }

        if (wait_threads_stopped) {
            while (!isStopped()) {
                hv_delay(1);
            }
        }
    }

    // @brief join all loop_threads
    // @note  destructor will join loop_threads if you forget to call this method.
    void join() {
        for (auto& loop_thread : loop_threads_) {
            loop_thread->join();
        }
    }

private:
    int                                         thread_num_;
    std::vector<EventLoopThreadPtr>             loop_threads_;
    std::atomic<unsigned int>                   next_loop_idx_;
};

}

#endif // HV_EVENT_LOOP_THREAD_POOL_HPP_
