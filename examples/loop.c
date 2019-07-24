#include "hloop.h"

void on_idle(hidle_t* idle) {
    printf("on_idle: event_id=%lu\tpriority=%d\tuserdata=%ld\n", idle->event_id, idle->priority, (long)idle->userdata);
}

void on_timer(htimer_t* timer) {
    printf("on_timer: event_id=%lu\tpriority=%d\tuserdata=%ld\ttime=%lus\thrtime=%luus\n",
        timer->event_id, timer->priority, (long)timer->userdata, hloop_now(timer->loop), timer->loop->cur_hrtime);
}

void cron_hourly(htimer_t* timer) {
    time_t tt;
    time(&tt);
    printf("cron_hourly: %s\n", ctime(&tt));
}

int main() {
    hloop_t loop;
    hloop_init(&loop);
    for (int i = HEVENT_LOWEST_PRIORITY; i <= HEVENT_HIGHEST_PRIORITY; ++i) {
        hidle_t* idle = hidle_add(&loop, on_idle, 10);
        idle->priority = i;
    }
    for (int i = 1; i <= 10; ++i) {
        htimer_t* timer = htimer_add(&loop, on_timer, i*1000, i);
        timer->userdata = (void*)i;
    }
    int minute = time(NULL)%3600/60;
    htimer_add_period(&loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);
    hloop_run(&loop);
    return 0;
}
