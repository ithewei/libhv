#include "hloop.h"
#include "hevent.h"
#include "iowatcher.h"

#include "hdef.h"
#include "hlog.h"
#include "hmath.h"

#define PAUSE_TIME              10      // ms
#define MIN_BLOCK_TIME          1       // ms
#define MAX_BLOCK_TIME          10000   // ms

#define IO_ARRAY_INIT_SIZE      64

static int timer_minheap_compare(const struct heap_node* lhs, const struct heap_node* rhs) {
    return TIMER_HEAP_ENTRY(lhs)->timeout < TIMER_HEAP_ENTRY(rhs)->timeout;
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
    struct list_node* node = loop->timers.next;
    htimer_t* timer = NULL;
    while (node != &loop->timers) {
        timer = TIMER_ENTRY(node);
        if (timer->destroy) goto destroy;
        if (!timer->active) goto next;
        if (timer->repeat == 0) {
            htimer_del(timer);
            //goto next;
            goto destroy;
        }
        if (loop->cur_hrtime > timer->next_timeout) {
            if (timer->repeat != INFINITE) {
                --timer->repeat;
            }
            timer->next_timeout += timer->timeout*1000;
            EVENT_PENDING(timer);
            ++ntimers;
        }
next:
        node = node->next;
        continue;
destroy:
        node = node->next;
        list_del(node->prev);
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

    int blocktime = MAX_BLOCK_TIME;
    if (loop->timer_minheap.root) {
        // if have timers, blocktime = min_timeout
        blocktime = TIMER_HEAP_ENTRY(loop->timer_minheap.root)->timeout;
        //if (!list_empty(&loop->idles))
            blocktime /= 10;
    }
    blocktime = LIMIT(MIN_BLOCK_TIME, blocktime, MAX_BLOCK_TIME);
    // if you want timer more precise, reduce blocktime

    uint64_t last_hrtime = loop->cur_hrtime;
    nios = hloop_process_ios(loop, blocktime);
    hloop_update_time(loop);
    ntimers = hloop_process_timers(loop);
    if (loop->npendings == 0) {
        loop->idle_time += last_hrtime - loop->cur_hrtime;
        // avoid frequent call idles
        if (loop->cur_hrtime - loop->last_idle_hrtime > 1e6) {
            loop->last_idle_hrtime = loop->cur_hrtime;
            nidles= hloop_process_idles(loop);
        }
        else {
            // hloop_process_ios maybe nonblock, so sleep here
            msleep(blocktime);
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
    list_init(&loop->timers);
    heap_init(&loop->timer_minheap, timer_minheap_compare);
    // iowatcher
    //iowatcher_init(loop);
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
    node = loop->timers.next;
    htimer_t* timer;
    while (node != &loop->timers) {
        timer = TIMER_ENTRY(node);
        node = node->next;
        free(timer);
    }
    list_init(&loop->timers);
    heap_init(&loop->timer_minheap, NULL);
    // iowatcher
    iowatcher_cleanup(loop);
};

int hloop_run(hloop_t* loop) {
    time(&loop->start_time);
    loop->start_hrtime = gethrtime();
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
    idle->repeat = repeat;
    list_add(&idle->node, &loop->idles);
    EVENT_ADD(loop, idle, cb);
    return idle;
}

void hidle_del(hidle_t* idle) {
    EVENT_DEL(idle);
}

htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint64_t timeout, uint32_t repeat) {
    htimer_t* timer = (htimer_t*)malloc(sizeof(htimer_t));
    memset(timer, 0, sizeof(htimer_t));
    timer->event_type = HEVENT_TYPE_TIMER;
    timer->repeat = repeat;
    timer->timeout = timeout;
    timer->next_timeout = gethrtime() + timeout*1000;
    list_add(&timer->node, &loop->timers);
    heap_insert(&loop->timer_minheap, &timer->hnode);
    EVENT_ADD(loop, timer, cb);
    return timer;
}

void htimer_del(htimer_t* timer) {
    heap_remove(&timer->loop->timer_minheap, &timer->hnode);
    EVENT_DEL(timer);
}

void hio_init(hio_t* io) {
    memset(io, 0, sizeof(hio_t));
    io->event_type = HEVENT_TYPE_IO;
    io->event_index[0] = io->event_index[1] = -1;
    // move to hwrite
    //write_queue_init(&io->write_queue, 4);;
};

void hio_cleanup(hio_t* io) {
    offset_buf_t* pbuf = NULL;
    while (!write_queue_empty(&io->write_queue)) {
        pbuf = write_queue_front(&io->write_queue);
        SAFE_FREE(pbuf->base);
        write_queue_pop_front(&io->write_queue);
    }
    write_queue_cleanup(&io->write_queue);
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
    iowatcher_del_event(io->loop, io->fd, events);
    io->events &= ~events;
    if (io->events == 0) {
        hio_cleanup(io);
        EVENT_DEL(io);
    }
}

