/*
 * @build: make examples
 * @usage: bin/hloop_test
 *         bin/nc 127.0.0.1 10514
 *         nc     127.0.0.1 10514
 *
 */

/*
 * @介绍：此程序是为了测试事件循环的各种事件，包括空闲事件、IO事件、定时器事件、自定义事件。
 *
 */

#include "hloop.h"
#include "hbase.h"
#include "hlog.h"
#include "nlog.h"

// 自定义日志处理器
void mylogger(int loglevel, const char* buf, int len) {
    // ERROR以上级别日志打印到标准错误
    if (loglevel >= LOG_LEVEL_ERROR) {
        stderr_logger(loglevel, buf, len);
    }

    // INFO以上级别日志写入日志文件
    if (loglevel >= LOG_LEVEL_INFO) {
        file_logger(loglevel, buf, len);
    }

    // 所有日志都发送到网络日志器
    network_logger(loglevel, buf, len);
}

void on_idle(hidle_t* idle) {
    printf("on_idle: event_id=%llu\tpriority=%d\tuserdata=%ld\n",
        LLU(hevent_id(idle)), hevent_priority(idle), (long)(intptr_t)(hevent_userdata(idle)));
}

void on_timer(htimer_t* timer) {
    hloop_t* loop = hevent_loop(timer);
    printf("on_timer: event_id=%llu\tpriority=%d\tuserdata=%ld\ttime=%llus\thrtime=%lluus\n",
        LLU(hevent_id(timer)), hevent_priority(timer), (long)(intptr_t)(hevent_userdata(timer)),
        LLU(hloop_now(loop)), LLU(hloop_now_hrtime(loop)));
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
    // 在程序退出时做内存检查
    // memcheck atexit
    HV_MEMCHECK;

    // 新建事件循环
    hloop_t* loop = hloop_new(0);

    // 测试空闲事件和事件优先级
    // test idle and priority
    for (int i = HEVENT_LOWEST_PRIORITY; i <= HEVENT_HIGHEST_PRIORITY; ++i) {
        hidle_t* idle = hidle_add(loop, on_idle, 10);
        hevent_set_priority(idle, i);
    }

    // 测试超时定时器
    // test timer timeout
    for (int i = 1; i <= 10; ++i) {
        htimer_t* timer = htimer_add(loop, on_timer, i*1000, 3);
        hevent_set_userdata(timer, (void*)(intptr_t)i);
    }

    // 测试时间定时器，类似cron效果
    // 获取下一分钟的分钟数，如30，则每小时的第30分钟会触发回调
    // test timer period
    int minute = time(NULL)%3600/60;
    htimer_add_period(loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);

    // 测试网络日志器，类似android logcat效果，可直接使用telent/nc连接端口10514接收日志
    // test network_logger
    htimer_add(loop, timer_write_log, 1000, INFINITE);
    logger_set_handler(hlog, mylogger);
    hlog_set_file("loop.log");
#ifndef _MSC_VER
    logger_enable_color(hlog, 1);
#endif
    nlog_listen(loop, DEFAULT_LOG_PORT);

    // 测试非阻塞标准输入
    // test nonblock stdin
    printf("input 'quit' to quit loop\n");
    char buf[64];
    hread(loop, 0, buf, sizeof(buf), on_stdin);

    // 测试自定义事件
    // test custom_events
    for (int i = 0; i < 10; ++i) {
        hevent_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.event_type = (hevent_type_e)(HEVENT_TYPE_CUSTOM + i);
        ev.cb = on_custom_events;
        ev.userdata = (void*)(long)i;
        hloop_post_event(loop, &ev);
    }

    // 运行事件循环
    hloop_run(loop);
    // 释放事件循环
    hloop_free(&loop);
    return 0;
}
