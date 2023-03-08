事件循环类

```c++

class EventLoop {

    // 返回底层的loop结构体指针
    hloop_t* loop();

    // 运行
    void run();
    // 停止
    void stop();
    // 暂停
    void pause();
    // 继续
    void resume();

    // 设置定时器
    TimerID setTimer(int timeout_ms, TimerCallback cb, uint32_t repeat = INFINITE, TimerID timerID = INVALID_TIMER_ID);

    // 设置一次性定时器
    TimerID setTimeout(int timeout_ms, TimerCallback cb);

    // 设置永久性定时器
    TimerID setInterval(int interval_ms, TimerCallback cb);

    // 杀掉定时器
    void killTimer(TimerID timerID);

    // 重置定时器
    void resetTimer(TimerID timerID, int timeout_ms = 0);

    // 返回事件循环所在的线程ID
    long tid();

    // 是否在事件循环所在线程
    bool isInLoopThread();

    // 断言在事件循环所在线程
    void assertInLoopThread();

    // 运行在事件循环里
    void runInLoop(Functor fn);

    // 队列在事件循环里
    void queueInLoop(Functor fn);

    // 投递一个事件到事件循环
    void postEvent(EventCallback cb);

};

class EventLoopThread {

    // 返回事件循环指针
    const EventLoopPtr& loop();

    // 返回底层的loop结构体指针
    hloop_t* hloop();

    // 是否运行中
    bool isRunning();

    /* 开始运行
     * wait_thread_started: 是否阻塞等待线程开始
     * pre:  线程开始后执行的函数
     * post: 线程结束前执行的函数
     */
    void start(bool wait_thread_started = true,
               Functor pre = Functor(),
               Functor post = Functor());

    // 停止运行
    void stop(bool wait_thread_stopped = false);

    // 等待线程退出
    void join();

};

class EventLoopThreadPool {

    // 获取线程数量
    int threadNum();

    // 设置线程数量
    void setThreadNum(int num);

    // 返回下一个事件循环对象
    // 支持轮询、随机、最少连接数等负载均衡策略
    EventLoopPtr nextLoop(load_balance_e lb = LB_RoundRobin);

    // 返回索引的事件循环对象
    EventLoopPtr loop(int idx = -1);

    // 返回索引的底层loop结构体指针
    hloop_t* hloop(int idx = -1);

    /* 开始运行
     * wait_threads_started: 是否阻塞等待所有线程开始
     * pre:  线程开始后执行的函数
     * post: 线程结束前执行的函数
     */
    void start(bool wait_threads_started = false,
               std::function<void(const EventLoopPtr&)> pre = NULL,
               std::function<void(const EventLoopPtr&)> post = NULL);

    // 停止运行
    void stop(bool wait_threads_stopped = false);

    // 等待所有线程退出
    void join();

};

```

测试代码见:

- [evpp/EventLoop_test.cpp](../../evpp/EventLoop_test.cpp)
- [evpp/EventLoopThread_test.cpp](../../evpp/EventLoopThread_test.cpp)
- [evpp/EventLoopThreadPool_test.cpp](../../evpp/EventLoopThreadPool_test.cpp)
