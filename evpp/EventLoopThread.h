#ifndef HV_EVENT_LOOP_THREAD_HPP_
#define HV_EVENT_LOOP_THREAD_HPP_

#include <thread>

#include "hlog.h"

#include "EventLoop.h"

namespace hv {

// EventLoopThread owns a background thread running one EventLoop.
class EventLoopThread : public Status {
public:
    // Return 0 means OK, other failed.
    typedef std::function<int()> Functor;

    EventLoopThread(EventLoopPtr loop = NULL) {
        setStatus(kInitializing);
        // is_loop_owner_ records whether this object created its own loop.
        // When an external loop is passed in, the caller owns that loop's
        // lifetime (and its thread), so subclasses must NOT stop it on their
        // own teardown. Exposed to subclasses via isLoopOwner() so the
        // "own loop -> stop it / external loop -> leave it" decision lives in
        // one place instead of a duplicated flag in every client/server class.
        //
        // CONTRACT (external loop): when an external loop is supplied, the
        // caller must have it already running (loop->run() on its own thread, or
        // published as this thread's running loop) BEFORE calling start(). This
        // is the normal usage (HttpServer IO loop, EventLoopThreadPool loop, the
        // Lua runtime loop obtained via currentThreadEventLoopPtr — which is only
        // non-NULL on an already-running loop thread). If instead an external,
        // not-yet-running loop is passed and start() is called, start() falls
        // back to spinning its OWN worker thread to drive that loop (see start()
        // below); stop() then joins that thread (thread_ != NULL) and does stop
        // the loop. That fallback is intentional and safe (used by
        // redis_async_client_test), NOT a bug — but it is not the intended path
        // for a loop the caller means to keep using elsewhere.
        is_loop_owner_ = (loop == NULL);
        loop_ = loop ? loop : std::make_shared<EventLoop>();
        setStatus(kInitialized);
    }

    ~EventLoopThread() {
        stop();
        join();
    }

    const EventLoopPtr& loop() {
        return loop_;
    }

    // Whether this object created (and therefore owns) its EventLoop. False when
    // an external loop was supplied at construction. Subclasses use this to
    // decide whether their stop() should also stop the loop/thread.
    bool isLoopOwner() const {
        return is_loop_owner_;
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
    // NOTE: if isRunning() is already true (the common case for an external loop
    // per the constructor's CONTRACT), start() is a no-op below and the loop is
    // driven by whoever already runs it. Only a not-yet-running loop reaches the
    // thread_ spin-up here (own loop, or the intentional external-loop fallback).
    void start(bool wait_thread_started = true,
               Functor pre = Functor(),
               Functor post = Functor()) {
        if (status() >= kStarting && status() < kStopped) return;
        if (isRunning()) return;
        setStatus(kStarting);

        thread_ = std::make_shared<std::thread>(&EventLoopThread::loop_thread, this, pre, post);

        if (wait_thread_started) {
            while (loop_->status() < kRunning) {
                hv_delay(1);
            }
        }
    }

    // @param wait_thread_started: if ture this method will block until loop_thread stopped.
    // stop thread-safe
    //
    // Ownership rule: stop() only stops the loop it is entitled to. A shared,
    // externally-supplied loop (is_loop_owner_ == false) must NOT be stopped
    // here — its creator owns that decision; a mere user has no right to stop
    // it. The one exception is when this object had to spin up its OWN thread
    // for an external loop that was not yet running (start() falls back to
    // EventLoopThread::start() then): that thread IS ours, so thread_ != NULL
    // and we must stop/join it to avoid a hang in the destructor's join().
    // So the guard is "own the loop, OR own a thread".
    void stop(bool wait_thread_stopped = false) {
        if (!is_loop_owner_ && !thread_) return;
        if (status() < kStarting || status() >= kStopping) return;
        setStatus(kStopping);

        long loop_tid = loop_->tid();
        loop_->stop();

        if (wait_thread_stopped) {
            if (hv_gettid() == loop_tid) return;
            join();
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
    bool                         is_loop_owner_;
};

typedef std::shared_ptr<EventLoopThread> EventLoopThreadPtr;

}

#endif // HV_EVENT_LOOP_THREAD_HPP_
