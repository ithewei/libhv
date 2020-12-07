#include "hloop.h"
#include "hbase.h"

void on_timer(htimer_t* timer) {
    printf("time=%llus on_timer\n", LLU(hloop_now(hevent_loop(timer))));
}

// test htimer_add
void on_timer_add(htimer_t* timer) {
    printf("time=%llus on_timer_add\n", LLU(hloop_now(hevent_loop(timer))));
    htimer_add(hevent_loop(timer), on_timer_add, 1000, 1);
}

// test htimer_del
void on_timer_del(htimer_t* timer) {
    printf("time=%llus on_timer_del\n", LLU(hloop_now(hevent_loop(timer))));
    htimer_del(timer);
}

// test htimer_reset
void on_timer_reset(htimer_t* timer) {
    printf("time=%llus on_timer_reset\n", LLU(hloop_now(hevent_loop(timer))));
    htimer_reset((htimer_t*)hevent_userdata(timer));
}

// test hloop_stop
void on_timer_quit(htimer_t* timer) {
    printf("time=%llus on_timer_quit\n", LLU(hloop_now(hevent_loop(timer))));
    hloop_stop(hevent_loop(timer));
}

// test cron
void cron_hourly(htimer_t* timer) {
    time_t tt = time(NULL);
    printf("time=%llus cron_hourly: %s\n", LLU(hloop_now(hevent_loop(timer))), ctime(&tt));
}

int main() {
    HV_MEMCHECK;
    hloop_t* loop = hloop_new(0);

    htimer_add(loop, on_timer_add, 1000, 1);
    htimer_add(loop, on_timer_del, 1000, 10);
    htimer_t* reseted = htimer_add(loop, on_timer, 5000, 1);
    htimer_t* reset = htimer_add(loop, on_timer_reset, 1000, 5);
    hevent_set_userdata(reset, reseted);

    // cron_hourly next triggered in one minute
    int minute = time(NULL)%3600/60;
    htimer_add_period(loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);

    // quit application after 1 min
    htimer_add(loop, on_timer_quit, 60000, 1);

    printf("time=%llus begin\n", LLU(hloop_now(loop)));
    hloop_run(loop);
    printf("time=%llus stop\n", LLU(hloop_now(loop)));
    hloop_free(&loop);
    return 0;
}
