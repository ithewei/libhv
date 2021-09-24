/*
 * EventLoopThread_test.cpp
 *
 * @build: make evpp
 *
 */

#include "hv.h"

#include "EventLoopThread.h"

using namespace hv;

static void onTimer(TimerID timerID, int n) {
    printf("tid=%ld timerID=%lu time=%lus n=%d\n", hv_gettid(), (unsigned long)timerID, (unsigned long)time(NULL), n);
}

int main(int argc, char* argv[]) {
    HV_MEMCHECK;

    printf("main tid=%ld\n", hv_gettid());

    EventLoopThread loop_thread;
    const EventLoopPtr& loop = loop_thread.loop();

    // runEvery 1s
    loop->setInterval(1000, std::bind(onTimer, std::placeholders::_1, 100));

    // runAfter 10s
    loop->setTimeout(10000, [&loop](TimerID timerID){
        loop->stop();
    });

    loop_thread.start();

    loop->queueInLoop([](){
        printf("queueInLoop tid=%ld\n", hv_gettid());
    });

    loop->runInLoop([](){
        printf("runInLoop tid=%ld\n", hv_gettid());
    });

    // wait loop_thread exit
    loop_thread.join();

    return 0;
}
