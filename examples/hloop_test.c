#include "hloop.h"
#include "hbase.h"
#include "nlog.h"

void mylogger(int loglevel, const char* buf, int len) {
    if (loglevel >= LOG_LEVEL_ERROR) {
        stderr_logger(loglevel, buf, len);
    }

    if (loglevel >= LOG_LEVEL_INFO) {
        file_logger(loglevel, buf, len);
    }

    network_logger(loglevel, buf, len);
}

void on_idle(hidle_t* idle) {
    printf("on_idle: event_id=%lu\tpriority=%d\tuserdata=%ld\n", hevent_id(idle), hevent_priority(idle), (long)(hevent_userdata(idle)));
}

void on_timer(htimer_t* timer) {
    hloop_t* loop = hevent_loop(timer);
    printf("on_timer: event_id=%lu\tpriority=%d\tuserdata=%ld\ttime=%lus\thrtime=%luus\n",
        hevent_id(timer), hevent_priority(timer), (long)(hevent_userdata(timer)), hloop_now(loop), hloop_now_hrtime(loop));
}

void cron_hourly(htimer_t* timer) {
    time_t tt;
    time(&tt);
    printf("cron_hourly: %s\n", ctime(&tt));
}

void timer_write_log(htimer_t* timer) {
    static int cnt = 0;
    hlogd("[%d] Do you recv me?", ++cnt);
    hlogi("[%d] Do you recv me?", ++cnt);
    hloge("[%d] Do you recv me?", ++cnt);
}

void on_stdin(hio_t* io, void* buf, int readbytes) {
    printf("on_stdin fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    printf("> %s\n", (char*)buf);
    if (strncmp((char*)buf, "quit", 4) == 0) {
        hloop_stop(hevent_loop(io));
    }
}

void on_custom_events(hevent_t* ev) {
    printf("on_custom_events event_type=%d userdata=%ld\n", (int)ev->event_type, (long)ev->userdata);
}

int main() {
    // memcheck atexit
    MEMCHECK;

    hloop_t* loop = hloop_new(0);

    // test idle and priority
    for (int i = HEVENT_LOWEST_PRIORITY; i <= HEVENT_HIGHEST_PRIORITY; ++i) {
        hidle_t* idle = hidle_add(loop, on_idle, 10);
        hevent_set_priority(idle, i);
    }

    // test timer timeout
    for (int i = 1; i <= 10; ++i) {
        htimer_t* timer = htimer_add(loop, on_timer, i*1000, 3);
        hevent_set_userdata(timer, (void*)(long)i);
    }

    // test timer period
    int minute = time(NULL)%3600/60;
    htimer_add_period(loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);

    // test network_logger
    htimer_add(loop, timer_write_log, 1000, INFINITE);
    logger_set_handler(hlog, mylogger);
    hlog_set_file("loop.log");
    logger_enable_color(hlog, 1);
    nlog_listen(loop, DEFAULT_LOG_PORT);

    // test nonblock stdin
    printf("input 'quit' to quit loop\n");
    char buf[64];
    hread(loop, 0, buf, sizeof(buf), on_stdin);

    // test custom_events
    for (int i = 0; i < 10; ++i) {
        hevent_t ev;
        ev.event_type = (hevent_type_e)(HEVENT_TYPE_CUSTOM + i);
        ev.cb = on_custom_events;
        ev.userdata = (void*)(long)i;
        hloop_post_event(loop, &ev);
    }

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
