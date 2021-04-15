#ifndef HV_THREAD_POOL_H_
#define HV_THREAD_POOL_H_

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include <memory>
#include <utility>

class HThreadPool {
public:
    using Task = std::function<void()>;

    HThreadPool(int size = std::thread::hardware_concurrency())
        : pool_size(size), idle_num(size), status(STOP) {
    }

    ~HThreadPool() {
        stop();
    }

    int start() {
        if (status == STOP) {
            status = RUNNING;
            for (int i = 0; i < pool_size; ++i) {
                workers.emplace_back(std::thread([this]{
                    while (status != STOP) {
                        while (status == PAUSE) {
                            std::this_thread::yield();
                        }

                        Task task;
                        {
                            std::unique_lock<std::mutex> locker(_mutex);
                            _cond.wait(locker, [this]{
                                return status == STOP || !tasks.empty();
                            });

                            if (status == STOP) return;

                            if (!tasks.empty()) {
                                --idle_num;
                                task = std::move(tasks.front());
                                tasks.pop();
                            }
                        }

                        task();
                        ++idle_num;
                    }
                }));
            }
        }
        return 0;
    }

    int stop() {
        if (status != STOP) {
            status = STOP;
            _cond.notify_all();
            for (auto& worker : workers) {
                worker.join();
            }
        }
        return 0;
    }

    int pause() {
        if (status == RUNNING) {
            status = PAUSE;
        }
        return 0;
    }

    int resume() {
        if (status == PAUSE) {
            status = RUNNING;
        }
        return 0;
    }

    int wait() {
        while (1) {
            if (status == STOP || (tasks.empty() && idle_num == pool_size)) {
                break;
            }
            std::this_thread::yield();
        }
        return 0;
    }

    // return a future, calling future.get() will wait task done and return RetType.
    // commit(fn, args...)
    // commit(std::bind(&Class::mem_fn, &obj))
    // commit(std::mem_fn(&Class::mem_fn, &obj))
    template<class Fn, class... Args>
    auto commit(Fn&& fn, Args&&... args) -> std::future<decltype(fn(args...))> {
        using RetType = decltype(fn(args...));
        auto task = std::make_shared<std::packaged_task<RetType()> >(
            std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        std::future<RetType> future = task->get_future();
        {
            std::lock_guard<std::mutex> locker(_mutex);
            tasks.emplace([task]{
                (*task)();
            });
        }

        _cond.notify_one();
        return future;
    }

public:
    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };
    int                 pool_size;
    std::atomic<int>    idle_num;
    std::atomic<Status> status;
    std::vector<std::thread>    workers;
    std::queue<Task>            tasks;

protected:
    std::mutex              _mutex;
    std::condition_variable _cond;
};

#endif // HV_THREAD_POOL_H_
