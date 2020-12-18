/*
 * EventLoop_test.cpp
 *
 * @build
 * make libhv && sudo make install
 * g++ -std=c++11 EventLoop_test.cpp -o EventLoop_test -I/usr/local/include/hv -lhv
 *
 */

#include "hbase.h" // import HV_MEMCHECK

#include "EventLoop.h"

using namespace hv;

static void onTimer(TimerID timerID, EventLoop* loop, int n) {
    static int cnt = 0;
    printf("n=%d timerID=%lu time=%lus\n", n, (unsigned long)timerID, (unsigned long)time(NULL));
    if (++cnt == 15) {
        printf("killTimer(%ld)\n", (unsigned long)timerID);
        loop->killTimer(timerID);
    }
}

int main(int argc, char* argv[]) {
    HV_MEMCHECK;

    EventLoop loop;

    int n = 100;
    loop.setInterval(1000, std::bind(onTimer, std::placeholders::_1, &loop, n));

    loop.setTimeout(10000, [&loop](TimerID timerID){
        static int cnt = 0;
        if (cnt++ == 0) {
            printf("resetTimer(%ld)\n", (unsigned long)timerID);
            loop.resetTimer(timerID);
        } else {
            loop.stop();
        }
    });

    printf("tid=%ld\n", hv_gettid());

    loop.queueInLoop([](){
        printf("queueInLoop tid=%ld\n", hv_gettid());
    });

    loop.runInLoop([](){
        printf("runInLoop tid=%ld\n", hv_gettid());
    });

    loop.start();

    return 0;
}
