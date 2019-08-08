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

void timer_write_log(htimer_t* timer) {
    static int cnt = 0;
    hlogd("[%d] Do you recv me?", ++cnt);
    hlogi("[%d] Do you recv me?", ++cnt);
    hloge("[%d] Do you recv me?", ++cnt);
}

void on_stdin(hio_t* io, void* buf, int readbytes) {
    printf("on_stdin fd=%d readbytes=%d\n", io->fd, readbytes);
    printf("> %s\n", buf);
    if (strncmp((char*)buf, "quit", 4) == 0) {
        hloop_stop(io->loop);
    }
}

int main() {
    // memcheck atexit
    MEMCHECK;

    hloop_t loop;
    hloop_init(&loop);

    // test idle and priority
    for (int i = HEVENT_LOWEST_PRIORITY; i <= HEVENT_HIGHEST_PRIORITY; ++i) {
        hidle_t* idle = hidle_add(&loop, on_idle, 10);
        idle->priority = i;
    }

    // test timer timeout
    for (int i = 1; i <= 10; ++i) {
        htimer_t* timer = htimer_add(&loop, on_timer, i*1000, 3);
        timer->userdata = (void*)i;
    }
    // test timer period
    int minute = time(NULL)%3600/60;
    htimer_add_period(&loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);

    // test network_logger
    htimer_add(&loop, timer_write_log, 1000, INFINITE);
    hlog_set_logger(mylogger);
    hlog_set_file("loop.log");
    nlog_listen(&loop, DEFAULT_LOG_PORT);

    // test nonblock stdin
    printf("input 'quit' to quit loop\n");
    char buf[64];
    hread(&loop, STDIN_FILENO, buf, sizeof(buf), on_stdin);

    hloop_run(&loop);
    nlog_close();
    return 0;
}
