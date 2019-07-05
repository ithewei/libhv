#include "hloop.h"

#include "hdef.h"
#include "htime.h"
#include "hevent.h"

static void hloop_update_time(hloop_t* loop) {
    loop->cur_time = gethrtime();
}

int hloop_init(hloop_t* loop) {
    loop->status = HLOOP_STATUS_STOP;
    loop->timer_counter = 0;
    loop->idle_counter = 0;
    loop->min_timer_timeout = INFINITE;
    loop->event_ctx = NULL;
    // hloop_event_init when add_event first
    // hloop_event_init(loop);
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
    for (auto& pair : loop->events) {
        hevent_t* event = pair.second;
        hloop_del_event(event);
        SAFE_FREE(event);
    }
    loop->events.clear();
    hloop_event_cleanup(loop);
}

int hloop_handle_timers(hloop_t* loop) {
    int ntimer = 0;
    auto iter = loop->timers.begin();
    while (iter != loop->timers.end()) {
        htimer_t* timer = iter->second;
        if (timer->destroy) goto destroy;
        if (timer->disable) goto next;
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
        if (idle->disable)  goto next;
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
#define MIN_EVENT_TIMEOUT       1       // ms
#define MAX_EVENT_TIMEOUT       1000    // ms
int hloop_run(hloop_t* loop) {
    int ntimer, nevent, nidle;
    int event_timeout;

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
        ntimer = nevent = nidle = 0;
        event_timeout = INFINITE;
        if (loop->timers.size() != 0) {
            ntimer = hloop_handle_timers(loop);
            event_timeout = MAX(MIN_EVENT_TIMEOUT, loop->min_timer_timeout/10);
        }
        if (loop->events.size() == 0 || loop->idles.size() != 0) {
            event_timeout = MIN(event_timeout, MAX_EVENT_TIMEOUT);
        }
        if (loop->events.size() != 0) {
            nevent = hloop_handle_events(loop, event_timeout);
        }
        else {
            msleep(event_timeout);
        }
        if (ntimer == 0 && nevent == 0 && loop->idles.size() != 0) {
            nidle = hloop_handle_idles(loop);
        }
        //printf("loop_cnt=%lu ntimer=%d nevent=%d nidle=%d\n", loop->loop_cnt, ntimer, nevent, nidle);
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
    timer->loop = loop;
    timer->timer_id = ++loop->timer_counter;
    timer->cb = cb;
    timer->userdata = userdata;
    timer->timeout = timeout;
    timer->repeat = repeat;
    timer->next_timeout = gethrtime() + timeout*1000;
    loop->timers[timer->timer_id] = timer;
    loop->min_timer_timeout = MIN(timeout, loop->min_timer_timeout);
    return timer;
}

void htimer_del(htimer_t* timer) {
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
    idle->loop = loop;
    idle->idle_id = ++loop->idle_counter;
    idle->cb = cb;
    idle->userdata = userdata;
    idle->repeat = repeat;
    loop->idles[idle->idle_id] = idle;
    return idle;
}

void hidle_del(hidle_t* idle) {
    idle->destroy = 1;
}

void hidle_del(hloop_t* loop, uint32_t idle_id) {
    auto iter = loop->idles.find(idle_id);
    if (iter != loop->idles.end()) {
        hidle_t* idle = iter->second;
        hidle_del(idle);
    }
}

hevent_t* hevent_add(hloop_t* loop, int fd) {
#ifdef EVENT_SELECT
    if (loop->events.size() >= FD_SETSIZE) return NULL;
#endif
    hevent_t* event = (hevent_t*)malloc(sizeof(hevent_t));
    memset(event, 0, sizeof(hevent_t));
    event->loop = loop;
    event->fd = fd;
    event->event_index = -1;
    loop->events[fd] = event;
    return event;
}

hevent_t* hevent_get(hloop_t* loop, int fd) {
    auto iter = loop->events.find(fd);
    if (iter != loop->events.end()) {
        return iter->second;
    }
    return NULL;
}

hevent_t* hevent_get_or_add(hloop_t* loop, int fd) {
    hevent_t* event = hevent_get(loop, fd);
    if (event)  {
        event->destroy = 0;
        event->disable = 0;
        return event;
    }
    return hevent_add(loop, fd);
}

void hevent_del(hevent_t* event) {
    event->destroy = 1;
    hloop_del_event(event, READ_EVENT|WRITE_EVENT);
}

void hevent_del(hloop_t* loop, int fd) {
    auto iter = loop->events.find(fd);
    if (iter != loop->events.end()) {
        hevent_del(iter->second);
    }
}

hevent_t* hevent_read(hloop_t* loop, int fd, hevent_cb cb, void* userdata) {
    hevent_t* event = hevent_get_or_add(loop, fd);
    if (event == NULL) return NULL;
    event->read_cb = cb;
    event->read_userdata = userdata;
    hloop_add_event(event, READ_EVENT);
    return event;
}

hevent_t* hevent_write(hloop_t* loop, int fd, hevent_cb cb, void* userdata) {
    hevent_t* event = hevent_get_or_add(loop, fd);
    if (event == NULL) return NULL;
    event->write_cb = cb;
    event->write_userdata = userdata;
    hloop_add_event(event, WRITE_EVENT);
    return event;
}

#include "hsocket.h"
hevent_t* hevent_accept(hloop_t* loop, int listenfd, hevent_cb cb, void* userdata) {
    hevent_t* event = hevent_read(loop, listenfd, cb, userdata);
    if (event) {
        nonblocking(listenfd);
        event->accept = 1;
    }
    return event;
}

hevent_t* hevent_connect(hloop_t* loop, int connfd, hevent_cb cb, void* userdata) {
    hevent_t* event = hevent_write(loop, connfd, cb, userdata);
    if (event) {
        nonblocking(connfd);
        event->connect = 1;
    }
    return event;
}

