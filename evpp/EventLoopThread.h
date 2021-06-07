#ifndef HV_EVENT_LOOP_THREAD_HPP_
#define HV_EVENT_LOOP_THREAD_HPP_

#include <thread>

#include "hlog.h"

#include "EventLoop.h"

namespace hv {

class EventLoopThread : public Status {
public:
    // Return 0 means OK, other failed.
    typedef std::function<int()> Functor;

    EventLoopThread(EventLoopPtr loop = NULL) {
        setStatus(kInitializing);
        if (loop) {
            loop_ = loop;
        } else {
            loop_.reset(new EventLoop);
        }
        setStatus(kInitialized);
    }

    ~EventLoopThread() {
        stop();
        join();
    }

    const EventLoopPtr& loop() {
        return loop_;
    }

    hloop_t* hloop() {
        return loop_->loop();
    }

    bool isRunning() {
        return loop_->isRunning();
    }

    // @param wait_thread_started: if ture this method will block until loop_thread started.
    // @param pre: This functor will be executed when loop_thread started.
    // @param post:This Functor will be executed when loop_thread stopped.
    void start(bool wait_thread_started = true,
               Functor pre = Functor(),
               Functor post = Functor()) {
        setStatus(kStarting);

        assert(thread_.get() == NULL);
        thread_.reset(new std::thread(&EventLoopThread::loop_thread, this, pre, post));

        if (wait_thread_started) {
            while (loop_->status() < kRunning) {
                hv_delay(1);
            }
        }
    }

    // @param wait_thread_started: if ture this method will block until loop_thread stopped.
    void stop(bool wait_thread_stopped = false) {
        if (status() >= kStopping) return;
        setStatus(kStopping);

        loop_->stop();

        if (wait_thread_stopped) {
            while (!isStopped()) {
                hv_delay(1);
            }
        }
    }

    // @brief join loop_thread
    // @note  destructor will join loop_thread if you forget to call this method.
    void join() {
        if (thread_ && thread_->joinable()) {
            thread_->join();
            thread_ = NULL;
        }
    }

private:
    void loop_thread(const Functor& pre, const Functor& post) {
        hlogi("EventLoopThread started, tid=%ld", hv_gettid());
        setStatus(kStarted);

        if (pre) {
            loop_->queueInLoop([this, pre]{
                if (pre() != 0) {
                    loop_->stop();
                }
            });
        }

        loop_->run();
        assert(loop_->isStopped());

        if (post) {
            post();
        }

        setStatus(kStopped);
        hlogi("EventLoopThread stopped, tid=%ld", hv_gettid());
    }

private:
    EventLoopPtr                 loop_;
    std::shared_ptr<std::thread> thread_;
};

typedef std::shared_ptr<EventLoopThread> EventLoopThreadPtr;

}

#endif // HV_EVENT_LOOP_THREAD_HPP_
