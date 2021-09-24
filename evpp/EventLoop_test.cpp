/*
 * EventLoop_test.cpp
 *
 * @build: make evpp
 *
 */

#include "hv.h"

#include "EventLoop.h"

using namespace hv;

static void onTimer(TimerID timerID, int n) {
    printf("tid=%ld timerID=%lu time=%lus n=%d\n", hv_gettid(), (unsigned long)timerID, (unsigned long)time(NULL), n);
}

int main(int argc, char* argv[]) {
    HV_MEMCHECK;

    printf("main tid=%ld\n", hv_gettid());

    EventLoopPtr loop(new EventLoop);

    // runEvery 1s
    loop->setInterval(1000, std::bind(onTimer, std::placeholders::_1, 100));

    // runAfter 10s
    loop->setTimeout(10000, [&loop](TimerID timerID){
        loop->stop();
    });

    loop->queueInLoop([](){
        printf("queueInLoop tid=%ld\n", hv_gettid());
    });

    loop->runInLoop([](){
        printf("runInLoop tid=%ld\n", hv_gettid());
    });

    // run until loop stopped
    loop->run();

    return 0;
}
