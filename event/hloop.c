#include "hloop.h"
#include "hevent.h"
#include "iowatcher.h"

#include "hdef.h"
#include "hlog.h"
#include "hmath.h"

#define PAUSE_TIME              10          // ms
#define MAX_BLOCK_TIME          1000        // ms

#define IO_ARRAY_INIT_SIZE      64

static void hio_init(hio_t* io);
static void hio_deinit(hio_t* io);
static void hio_free(hio_t* io);

static int timers_compare(const struct heap_node* lhs, const struct heap_node* rhs) {
    return TIMER_ENTRY(lhs)->next_timeout < TIMER_ENTRY(rhs)->next_timeout;
}

static int hloop_process_idles(hloop_t* loop) {
    int nidles = 0;
    struct list_node* node = loop->idles.next;
    hidle_t* idle = NULL;
    while (node != &loop->idles) {
        idle = IDLE_ENTRY(node);
        if (idle->destroy) goto destroy;
        if (!idle->active) goto next;
        if (idle->repeat == 0) {
            hidle_del(idle);
            //goto next;
            goto destroy;
        }
        if (idle->repeat != INFINITE) {
            --idle->repeat;
        }
        EVENT_PENDING(idle);
        ++nidles;
next:
        node = node->next;
        continue;
destroy:
        node = node->next;
        list_del(node->prev);
        free(idle);
    }
    return nidles;
}

static int hloop_process_timers(hloop_t* loop) {
    int ntimers = 0;
    htimer_t* timer = NULL;
    uint64_t now_hrtime = hloop_now_hrtime(loop);
    while (loop->timers.root) {
        timer = TIMER_ENTRY(loop->timers.root);
        if (timer->destroy) goto destroy;
        if (timer->repeat == 0) {
            htimer_del(timer);
            goto destroy;
        }
        if (timer->next_timeout > now_hrtime) {
            break;
        }
        if (timer->repeat != INFINITE) {
            --timer->repeat;
        }
        heap_dequeue(&loop->timers);
        if (timer->event_type == HEVENT_TYPE_TIMEOUT) {
            timer->next_timeout += ((htimeout_t*)timer)->timeout*1000;
        }
        else if (timer->event_type == HEVENT_TYPE_PERIOD) {
            hperiod_t* period = (hperiod_t*)timer;
            timer->next_timeout = calc_next_timeout(period->minute, period->hour, period->day,
                    period->week, period->month) * 1e6;
        }
        heap_insert(&loop->timers, &timer->node);
        EVENT_PENDING(timer);
        ++ntimers;
        continue;
destroy:
        heap_dequeue(&loop->timers);
        free(timer);
    }
    return ntimers;
}

static int hloop_process_ios(hloop_t* loop, int timeout) {
    int nevents = iowatcher_poll_events(loop, timeout);
    if (nevents < 0) {
        hloge("poll_events error=%d", -nevents);
    }
    return nevents < 0 ? 0 : nevents;
}

static int hloop_process_pendings(hloop_t* loop) {
    if (loop->npendings == 0) return 0;

    hevent_t* prev = NULL;
    hevent_t* next = NULL;
    int ncbs = 0;
    for (int i = HEVENT_PRIORITY_SIZE-1; i >= 0; --i) {
        next = loop->pendings[i];
        while (next) {
            if (next->active && next->cb) {
                next->cb(next);
                ++ncbs;
            }
            prev = next;
            next = next->pending_next;
            prev->pending = 0;
            prev->pending_next = NULL;
        }
        loop->pendings[i] = NULL;
    }
    loop->npendings = 0;
    return ncbs;
}

static int hloop_process_events(hloop_t* loop) {
    // ios -> timers -> idles
    int nios, ntimers, nidles;
    nios = ntimers = nidles = 0;

    int32_t blocktime = MAX_BLOCK_TIME;
    hloop_update_time(loop);
    if (loop->timers.root) {
        uint64_t next_min_timeout = TIMER_ENTRY(loop->timers.root)->next_timeout;
        blocktime = next_min_timeout - hloop_now_hrtime(loop);
        if (blocktime <= 0) goto process_timers;
        blocktime /= 1000;
        ++blocktime;
        blocktime = MIN(blocktime, MAX_BLOCK_TIME);
    }

    if (loop->nios) {
        nios = hloop_process_ios(loop, blocktime);
    }
    else {
        msleep(blocktime);
    }
    hloop_update_time(loop);

process_timers:
    if (loop->ntimers) {
        ntimers = hloop_process_timers(loop);
    }

    if (loop->npendings == 0) {
        if (loop->nidles) {
            nidles= hloop_process_idles(loop);
        }
    }
    printd("blocktime=%d nios=%d ntimers=%d nidles=%d nactives=%d npendings=%d\n", blocktime, nios, ntimers, nidles, loop->nactives, loop->npendings);
    return hloop_process_pendings(loop);
}

int hloop_init(hloop_t* loop) {
    memset(loop, 0, sizeof(hloop_t));
    loop->status = HLOOP_STATUS_STOP;
    // idles
    list_init(&loop->idles);
    // timers
    heap_init(&loop->timers, timers_compare);
    // ios: init when hio_add
    //io_array_init(&loop->ios, IO_ARRAY_INIT_SIZE);
    // iowatcher: init when iowatcher_add_event
    //iowatcher_init(loop);
    // time
    time(&loop->start_time);
    loop->start_hrtime = loop->cur_hrtime = gethrtime();
    return 0;
}

void hloop_cleanup(hloop_t* loop) {
    // pendings
    for (int i = 0; i < HEVENT_PRIORITY_SIZE; ++i) {
        loop->pendings[i] = NULL;
    }
    // idles
    struct list_node* node = loop->idles.next;
    hidle_t* idle;
    while (node != &loop->idles) {
        idle = IDLE_ENTRY(node);
        node = node->next;
        free(idle);
    }
    list_init(&loop->idles);
    // timers
    htimer_t* timer;
    while (loop->timers.root) {
        timer = TIMER_ENTRY(node);
        heap_dequeue(&loop->timers);
        free(timer);
    }
    heap_init(&loop->timers, NULL);
    // ios
    for (int i = 0; i < loop->ios.maxsize; ++i) {
        hio_t* io = loop->ios.ptr[i];
        if (io) {
            hio_free(io);
        }
    }
    io_array_cleanup(&loop->ios);
    // iowatcher
    iowatcher_cleanup(loop);
};

int hloop_run(hloop_t* loop) {
    loop->loop_cnt = 0;
    loop->status = HLOOP_STATUS_RUNNING;
    while (loop->status != HLOOP_STATUS_STOP) {
        if (loop->status == HLOOP_STATUS_PAUSE) {
            msleep(PAUSE_TIME);
            hloop_update_time(loop);
            continue;
        }
        ++loop->loop_cnt;
        if (loop->nactives == 0) break;
        hloop_process_events(loop);
    }
    loop->status = HLOOP_STATUS_STOP;
    loop->end_hrtime = gethrtime();
    hloop_cleanup(loop);
    return 0;
};

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

hidle_t* hidle_add(hloop_t* loop, hidle_cb cb, uint32_t repeat) {
    hidle_t* idle = (hidle_t*)malloc(sizeof(hidle_t));
    memset(idle, 0, sizeof(hidle_t));
    idle->event_type = HEVENT_TYPE_IDLE;
    idle->priority = HEVENT_LOWEST_PRIORITY;
    idle->repeat = repeat;
    list_add(&idle->node, &loop->idles);
    EVENT_ADD(loop, idle, cb);
    loop->nidles++;
    return idle;
}

void hidle_del(hidle_t* idle) {
    if (idle->destroy) return;
    idle->loop->nidles--;
    EVENT_DEL(idle);
}

htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint64_t timeout, uint32_t repeat) {
    if (timeout == 0)   return NULL;
    htimeout_t* timer = (htimeout_t*)malloc(sizeof(htimeout_t));
    memset(timer, 0, sizeof(htimeout_t));
    timer->event_type = HEVENT_TYPE_TIMEOUT;
    timer->priority = HEVENT_HIGHEST_PRIORITY;
    timer->repeat = repeat;
    timer->timeout = timeout;
    hloop_update_time(loop);
    timer->next_timeout = hloop_now_hrtime(loop) + timeout*1000;
    heap_insert(&loop->timers, &timer->node);
    EVENT_ADD(loop, timer, cb);
    loop->ntimers++;
    return (htimer_t*)timer;
}

htimer_t* htimer_add_period(hloop_t* loop, htimer_cb cb,
                int8_t minute,  int8_t hour, int8_t day,
                int8_t week, int8_t month, uint32_t repeat) {
    if (minute > 59 || hour > 23 || day > 31 || week > 6 || month > 12) {
        return NULL;
    }
    hperiod_t* timer = (hperiod_t*)malloc(sizeof(hperiod_t));
    memset(timer, 0, sizeof(hperiod_t));
    timer->event_type = HEVENT_TYPE_PERIOD;
    timer->priority = HEVENT_HIGH_PRIORITY;
    timer->repeat = repeat;
    timer->minute = minute;
    timer->hour   = hour;
    timer->day    = day;
    timer->month  = month;
    timer->week   = week;
    timer->next_timeout = calc_next_timeout(minute, hour, day, week, month) * 1e6;
    heap_insert(&loop->timers, &timer->node);
    EVENT_ADD(loop, timer, cb);
    loop->ntimers++;
    return (htimer_t*)timer;
}

void htimer_del(htimer_t* timer) {
    if (timer->destroy) return;
    timer->loop->ntimers--;
    EVENT_DEL(timer);
}

void hio_init(hio_t* io) {
    memset(io, 0, sizeof(hio_t));
    io->event_type = HEVENT_TYPE_IO;
    io->event_index[0] = io->event_index[1] = -1;
    // write_queue init when hwrite try_write failed
    //write_queue_init(&io->write_queue, 4);;
}

void hio_deinit(hio_t* io) {
    offset_buf_t* pbuf = NULL;
    while (!write_queue_empty(&io->write_queue)) {
        pbuf = write_queue_front(&io->write_queue);
        SAFE_FREE(pbuf->base);
        write_queue_pop_front(&io->write_queue);
    }
    write_queue_cleanup(&io->write_queue);
}

void hio_free(hio_t* io) {
    if (io == NULL) return;
    hio_deinit(io);
    SAFE_FREE(io->localaddr);
    SAFE_FREE(io->peeraddr);
    free(io);
}

hio_t* hio_add(hloop_t* loop, hio_cb cb, int fd, int events) {
    if (loop->ios.maxsize == 0) {
        io_array_init(&loop->ios, IO_ARRAY_INIT_SIZE);
    }

    if (fd >= loop->ios.maxsize) {
        int newsize = ceil2e(fd);
        io_array_resize(&loop->ios, newsize > fd ? newsize : 2*fd);
    }

    hio_t* io = loop->ios.ptr[fd];
    if (io == NULL) {
        io = (hio_t*)malloc(sizeof(hio_t));
        memset(io, 0, sizeof(hio_t));
        loop->ios.ptr[fd] = io;
    }

    if (!io->active || io->destroy) {
        hio_init(io);
        EVENT_ADD(loop, io, cb);
        loop->nios++;
    }

    io->fd = fd;
    if (cb) {
        io->cb = (hevent_cb)cb;
    }
    iowatcher_add_event(loop, fd, events);
    io->events |= events;
    return io;
}

void hio_del(hio_t* io, int events) {
    if (io->destroy) return;
    iowatcher_del_event(io->loop, io->fd, events);
    io->events &= ~events;
    if (io->events == 0) {
        io->loop->nios--;
        hio_deinit(io);
        EVENT_DEL(io);
    }
}

#include "hsocket.h"
hio_t* hlisten (hloop_t* loop, int port, haccept_cb accept_cb) {
    int listenfd = Listen(port);
    if (listenfd < 0) {
        return NULL;
    }
    hio_t* io = haccept(loop, listenfd, accept_cb);
    if (io == NULL) {
        closesocket(listenfd);
    }
    return io;
}
