/*
 * EventLoopThreadPool_test.cpp
 *
 * @build
 * make libhv && sudo make install
 * g++ -std=c++11 EventLoopThreadPool_test.cpp -o EventLoopThreadPool_test -I/usr/local/include/hv -lhv -lpthread
 *
 */

#include "hv.h"

#include "EventLoopThreadPool.h"

using namespace hv;

static void onTimer(TimerID timerID, int n) {
    printf("tid=%ld timerID=%lu time=%lus n=%d\n", hv_gettid(), (unsigned long)timerID, (unsigned long)time(NULL), n);
}

int main(int argc, char* argv[]) {
    HV_MEMCHECK;

    printf("main tid=%ld\n", hv_gettid());

    EventLoopPtr master_loop(new EventLoop);
    EventLoopThreadPool loop_threads(master_loop, 4);

    loop_threads.start(true);

    int thread_num = loop_threads.thread_num();
    for (int i = 0; i < thread_num; ++i) {
        EventLoopPtr loop = loop_threads.nextLoop();
        printf("worker[%d] tid=%ld\n", i, loop->tid());

        loop->runInLoop([loop](){
            // runEvery 1s
            loop->setInterval(1000, std::bind(onTimer, std::placeholders::_1, 100));
        });

        loop->queueInLoop([](){
            printf("queueInLoop tid=%ld\n", hv_gettid());
        });

        loop->runInLoop([](){
            printf("runInLoop tid=%ld\n", hv_gettid());
        });
    }

    // runAfter 10s
    master_loop->setTimeout(10000, [&loop_threads](TimerID timerID){
        EventLoopPtr master_loop = loop_threads.loop();
        master_loop->stop();
        loop_threads.stop(false);
    });

    // master_loop run in main thread
    master_loop->run();

    // wait loop_threads exit
    loop_threads.join();

    return 0;
}
