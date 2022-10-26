#ifndef HV_THREAD_POOL_H_
#define HV_THREAD_POOL_H_

/*
 * @usage unittest/threadpool_test.cpp
 */

#include <time.h>
#include <thread>
#include <list>
#include <queue>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include <memory>
#include <utility>
#include <chrono>

#define DEFAULT_THREAD_POOL_MIN_THREAD_NUM  1
#define DEFAULT_THREAD_POOL_MAX_THREAD_NUM  std::thread::hardware_concurrency()
#define DEFAULT_THREAD_POOL_MAX_IDLE_TIME   60000 // ms

class HThreadPool {
public:
    using Task = std::function<void()>;

    HThreadPool(int min_threads = DEFAULT_THREAD_POOL_MIN_THREAD_NUM,
                int max_threads = DEFAULT_THREAD_POOL_MAX_THREAD_NUM,
                int max_idle_ms = DEFAULT_THREAD_POOL_MAX_IDLE_TIME)
        : min_thread_num(min_threads)
        , max_thread_num(max_threads)
        , max_idle_time(max_idle_ms)
        , status(STOP)
        , cur_thread_num(0)
        , idle_thread_num(0)
    {}

    virtual ~HThreadPool() {
        stop();
    }

    void setMinThreadNum(int min_threads) {
        min_thread_num = min_threads;
    }
    void setMaxThreadNum(int max_threads) {
        max_thread_num = max_threads;
    }
    void setMaxIdleTime(int ms) {
        max_idle_time = ms;
    }
    int currentThreadNum() {
        return cur_thread_num;
    }
    int idleThreadNum() {
        return idle_thread_num;
    }
    size_t taskNum() {
        std::lock_guard<std::mutex> locker(task_mutex);
        return tasks.size();
    }
    bool isStarted() {
        return status != STOP;
    }
    bool isStopped() {
        return status == STOP;
    }

    int start(int start_threads = 0) {
        if (status != STOP) return -1;
        status = RUNNING;
        if (start_threads < min_thread_num) start_threads = min_thread_num;
        if (start_threads > max_thread_num) start_threads = max_thread_num;
        for (int i = 0; i < start_threads; ++i) {
            createThread();
        }
        return 0;
    }

    int stop() {
        if (status == STOP) return -1;
        status = STOP;
        task_cond.notify_all();
        for (auto& i : threads) {
            if (i.thread->joinable()) {
                i.thread->join();
            }
        }
        threads.clear();
        cur_thread_num = 0;
        idle_thread_num = 0;
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
        while (status != STOP) {
            if (tasks.empty() && idle_thread_num == cur_thread_num) {
                break;
            }
            std::this_thread::yield();
        }
        return 0;
    }

    /*
     * return a future, calling future.get() will wait task done and return RetType.
     * commit(fn, args...)
     * commit(std::bind(&Class::mem_fn, &obj))
     * commit(std::mem_fn(&Class::mem_fn, &obj))
     *
     */
    template<class Fn, class... Args>
    auto commit(Fn&& fn, Args&&... args) -> std::future<decltype(fn(args...))> {
        if (status == STOP) start();
        if (idle_thread_num <= tasks.size() && cur_thread_num < max_thread_num) {
            createThread();
        }
        using RetType = decltype(fn(args...));
        auto task = std::make_shared<std::packaged_task<RetType()> >(
            std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        std::future<RetType> future = task->get_future();
        {
            std::lock_guard<std::mutex> locker(task_mutex);
            tasks.emplace([task]{
                (*task)();
            });
        }

        task_cond.notify_one();
        return future;
    }

protected:
    bool createThread() {
        if (cur_thread_num >= max_thread_num) return false;
        std::thread* thread = new std::thread([this] {
            while (status != STOP) {
                while (status == PAUSE) {
                    std::this_thread::yield();
                }

                Task task;
                {
                    std::unique_lock<std::mutex> locker(task_mutex);
                    task_cond.wait_for(locker, std::chrono::milliseconds(max_idle_time), [this]() {
                        return status == STOP || !tasks.empty();
                    });
                    if (status == STOP) return;
                    if (tasks.empty()) {
                        if (cur_thread_num > min_thread_num) {
                            delThread(std::this_thread::get_id());
                            return;
                        }
                        continue;
                    }
                    --idle_thread_num;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                if (task) {
                    task();
                    ++idle_thread_num;
                }
            }
        });
        addThread(thread);
        return true;
    }

    void addThread(std::thread* thread) {
        thread_mutex.lock();
        ++cur_thread_num;
        ++idle_thread_num;
        ThreadData data;
        data.thread = std::shared_ptr<std::thread>(thread);
        data.id = thread->get_id();
        data.status = RUNNING;
        data.start_time = time(NULL);
        data.stop_time = 0;
        threads.emplace_back(data);
        thread_mutex.unlock();
    }

    void delThread(std::thread::id id) {
        time_t now = time(NULL);
        thread_mutex.lock();
        --cur_thread_num;
        --idle_thread_num;
        auto iter = threads.begin();
        while (iter != threads.end()) {
            if (iter->status == STOP && now > iter->stop_time) {
                if (iter->thread->joinable()) {
                    iter->thread->join();
                    iter = threads.erase(iter);
                    continue;
                }
            } else if (iter->id == id) {
                iter->status = STOP;
                iter->stop_time = time(NULL);
            }
            ++iter;
        }
        thread_mutex.unlock();
    }

public:
    int min_thread_num;
    int max_thread_num;
    int max_idle_time;

protected:
    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };
    struct ThreadData {
        std::shared_ptr<std::thread> thread;
        std::thread::id id;
        Status          status;
        time_t          start_time;
        time_t          stop_time;
    };
    std::atomic<Status>     status;
    std::atomic<int>        cur_thread_num;
    std::atomic<int>        idle_thread_num;
    std::list<ThreadData>   threads;
    std::mutex              thread_mutex;
    std::queue<Task>        tasks;
    std::mutex              task_mutex;
    std::condition_variable task_cond;
};

#endif // HV_THREAD_POOL_H_
