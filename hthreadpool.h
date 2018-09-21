#ifndef H_THREAD_POOL_H
#define H_THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>

#include "hlog.h"
#include "hthread.h"

class HThreadPool{
public:
    using Task = std::function<void()>;

    HThreadPool(int size = std::thread::hardware_concurrency()) : pool_size(size), idle_num(size), status(STOP){

    }

    ~HThreadPool(){
        stop();
    }

    int start() {
        if (status == STOP) {
            status = RUNNING;
            for (int i = 0; i < pool_size; ++i){
                workers.emplace_back(std::thread([this]{
                    hlogd("work thread[%X] running...", gettid());
                    while (status != STOP){
                        while (status == PAUSE){
                            std::this_thread::yield();
                        }

                        Task task;
                        {
                            std::unique_lock<std::mutex> locker(mutex);
                            cond.wait(locker, [this]{
                                return status == STOP || !tasks.empty();
                            });

                            if (status == STOP) return;
                            
                            if (!tasks.empty()){
                                task = std::move(tasks.front());
                                tasks.pop();
                            }
                        }

                        --idle_num;
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
            cond.notify_all();
            for (auto& thread: workers){
                thread.join();
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

    // return a future, calling future.get() will wait task done and return RetType.
    // commit(fn, args...)
    // commit(std::bind(&Class::mem_fn, &obj))
    // commit(std::mem_fn(&Class::mem_fn, &obj))
    template<class Fn, class... Args>
    auto commit(Fn&& fn, Args&&... args) -> std::future<decltype(fn(args...))>{
        using RetType = decltype(fn(args...));
        auto task = std::make_shared<std::packaged_task<RetType()> >(
            std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...)
        );
        std::future<RetType> future = task->get_future();
        {
            std::lock_guard<std::mutex> locker(mutex);
            tasks.emplace([task]{
                (*task)();
            });
        }
        
        cond.notify_one();
        return future;
    }

public:
    int pool_size;
    std::atomic<int> idle_num;

    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };
    std::atomic<Status> status;
    std::vector<std::thread> workers;

    std::queue<Task> tasks;
    std::mutex        mutex;
    std::condition_variable cond;
};

#endif // H_THREAD_POOL_H