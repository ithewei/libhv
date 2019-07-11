#include "hloop.h"
#include "hio.h"
#include "io_watcher.h"

#include "hdef.h"
#include "htime.h"

static void hloop_update_time(hloop_t* loop) {
    loop->cur_time = gethrtime();
}

int hloop_init(hloop_t* loop) {
    loop->status = HLOOP_STATUS_STOP;
    loop->event_counter = 0;
    loop->timer_counter = 0;
    loop->idle_counter = 0;
    loop->min_timer_timeout = INFINITE;
    loop->iowatcher = NULL;
    //hloop_iowatcher_init(loop);
    return 0;
}

void hloop_cleanup(hloop_t* loop) {
    for (auto& pair : loop->timers) {
        SAFE_FREE(pair.second);
    }
    loop->timers.clear();
    for (auto& pair : loop->idles) {
        SAFE_FREE(pair.second);
    }
    loop->idles.clear();
    for (auto& pair : loop->ios) {
        hio_t* io = pair.second;
        hio_del(io);
        SAFE_FREE(io);
    }
    loop->ios.clear();
    hloop_iowatcher_cleanup(loop);
}

int hloop_handle_timers(hloop_t* loop) {
    int ntimer = 0;
    auto iter = loop->timers.begin();
    while (iter != loop->timers.end()) {
        htimer_t* timer = iter->second;
        if (timer->destroy) goto destroy;
        if (!timer->active) goto next;
        if (timer->repeat == 0) goto destroy;
        if (timer->next_timeout < loop->cur_time) {
            ++ntimer;
            if (timer->cb) {
                timer->cb(timer, timer->userdata);
            }
            timer->next_timeout += timer->timeout*1000;
            if (timer->repeat != INFINITE) {
                --timer->repeat;
            }
        }
next:
        ++iter;
        continue;
destroy:
        free(timer);
        iter = loop->timers.erase(iter);
    }
    return ntimer;
}

int hloop_handle_idles(hloop_t* loop) {
    int nidle = 0;
    auto iter = loop->idles.begin();
    while (iter != loop->idles.end()) {
        hidle_t* idle = iter->second;
        if (idle->destroy)  goto destroy;
        if (!idle->active)  goto next;
        if (idle->repeat == 0) goto destroy;
        ++nidle;
        if (idle->cb) {
            idle->cb(idle, idle->userdata);
        }
        if (idle->repeat != INFINITE) {
            --idle->repeat;
        }
next:
        ++iter;
        continue;
destroy:
        free(idle);
        iter = loop->idles.erase(iter);
    }
    return nidle;
}

#define PAUSE_SLEEP_TIME        10      // ms
#define MIN_POLL_TIMEOUT        1       // ms
#define MAX_POLL_TIMEOUT        1000    // ms
int hloop_run(hloop_t* loop) {
    int ntimer, nio, nidle;
    int poll_timeout;

    loop->start_time = gethrtime();
    loop->status = HLOOP_STATUS_RUNNING;
    loop->loop_cnt = 0;
    while (loop->status != HLOOP_STATUS_STOP) {
        hloop_update_time(loop);
        if (loop->status == HLOOP_STATUS_PAUSE) {
            msleep(PAUSE_SLEEP_TIME);
            continue;
        }
        ++loop->loop_cnt;
        // timers -> events -> idles
        ntimer = nio = nidle = 0;
        poll_timeout = INFINITE;
        if (loop->timers.size() != 0) {
            ntimer = hloop_handle_timers(loop);
            poll_timeout = MAX(MIN_POLL_TIMEOUT, loop->min_timer_timeout/10);
        }
        if (loop->ios.size() == 0 || loop->idles.size() != 0) {
            poll_timeout = MIN(poll_timeout, MAX_POLL_TIMEOUT);
        }
        if (loop->ios.size() != 0) {
            nio = hloop_handle_ios(loop, poll_timeout);
        }
        else {
            msleep(poll_timeout);
        }
        if (ntimer == 0 && nio == 0 && loop->idles.size() != 0) {
            nidle = hloop_handle_idles(loop);
        }
        //printf("loop_cnt=%lu ntimer=%d nio=%d nidle=%d\n", loop->loop_cnt, ntimer, nio, nidle);
    }
    loop->status = HLOOP_STATUS_STOP;
    loop->end_time = gethrtime();
    hloop_cleanup(loop);
    return 0;
}

int hloop_stop(hloop_t* loop) {
    loop->status = HLOOP_STATUS_STOP;
    return 0;
}

int hloop_pause(hloop_t* loop) {
    if (loop->status == HLOOP_STATUS_RUNNING) {
        loop->status = HLOOP_STATUS_PAUSE;
    }
    return 0;
}

int hloop_resume(hloop_t* loop) {
    if (loop->status == HLOOP_STATUS_PAUSE) {
        loop->status = HLOOP_STATUS_RUNNING;
    }
    return 0;
}

htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, void* userdata, uint64_t timeout, uint32_t repeat) {
    htimer_t* timer = (htimer_t*)malloc(sizeof(htimer_t));
    memset(timer, 0, sizeof(htimer_t));
    timer->event_type = HEVENT_TYPE_TIMER;
    timer->event_id = ++loop->event_counter;
    timer->loop = loop;
    timer->timer_id = ++loop->timer_counter;
    timer->cb = cb;
    timer->userdata = userdata;
    timer->timeout = timeout;
    timer->repeat = repeat;
    timer->next_timeout = gethrtime() + timeout*1000;
    timer->active = 1;
    loop->timers[timer->timer_id] = timer;
    loop->min_timer_timeout = MIN(timeout, loop->min_timer_timeout);
    return timer;
}

void htimer_del(htimer_t* timer) {
    timer->active = 0;
    timer->destroy = 1;
}

void htimer_del(hloop_t* loop, uint32_t timer_id) {
    auto iter = loop->timers.find(timer_id);
    if (iter != loop->timers.end()) {
        htimer_t* timer = iter->second;
        htimer_del(timer);
    }
}

hidle_t* hidle_add(hloop_t* loop, hidle_cb cb, void* userdata, uint32_t repeat) {
    hidle_t* idle = (hidle_t*)malloc(sizeof(hidle_t));
    memset(idle, 0, sizeof(hidle_t));
    idle->event_type = HEVENT_TYPE_IDLE;
    idle->event_id = ++loop->event_counter;
    idle->loop = loop;
    idle->idle_id = ++loop->idle_counter;
    idle->cb = cb;
    idle->userdata = userdata;
    idle->repeat = repeat;
    idle->active = 1;
    loop->idles[idle->idle_id] = idle;
    return idle;
}

void hidle_del(hidle_t* idle) {
    idle->active = 0;
    idle->destroy = 1;
}

void hidle_del(hloop_t* loop, uint32_t idle_id) {
    auto iter = loop->idles.find(idle_id);
    if (iter != loop->idles.end()) {
        hidle_t* idle = iter->second;
        hidle_del(idle);
    }
}
