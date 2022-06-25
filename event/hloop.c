#include "hloop.h"
#include "hevent.h"
#include "iowatcher.h"

#include "hdef.h"
#include "hbase.h"
#include "hlog.h"
#include "hmath.h"
#include "htime.h"
#include "hsocket.h"
#include "hthread.h"

#if defined(OS_UNIX) && HAVE_EVENTFD
#include "sys/eventfd.h"
#endif

#define HLOOP_PAUSE_TIME        10      // ms
#define HLOOP_MAX_BLOCK_TIME    100     // ms
#define HLOOP_STAT_TIMEOUT      60000   // ms

#define IO_ARRAY_INIT_SIZE              1024
#define CUSTOM_EVENT_QUEUE_INIT_SIZE    16

#define EVENTFDS_READ_INDEX     0
#define EVENTFDS_WRITE_INDEX    1

static void __hidle_del(hidle_t* idle);
static void __htimer_del(htimer_t* timer);

static int timers_compare(const struct heap_node* lhs, const struct heap_node* rhs) {
    return TIMER_ENTRY(lhs)->next_timeout < TIMER_ENTRY(rhs)->next_timeout;
}

static int hloop_process_idles(hloop_t* loop) {
    int nidles = 0;
    struct list_node* node = loop->idles.next;
    hidle_t* idle = NULL;
    while (node != &loop->idles) {
        idle = IDLE_ENTRY(node);
        node = node->next;
        if (idle->repeat != INFINITE) {
            --idle->repeat;
        }
        if (idle->repeat == 0) {
            // NOTE: Just mark it as destroy and remove from list.
            // Real deletion occurs after hloop_process_pendings.
            __hidle_del(idle);
        }
        EVENT_PENDING(idle);
        ++nidles;
    }
    return nidles;
}

static int __hloop_process_timers(struct heap* timers, uint64_t timeout) {
    int ntimers = 0;
    htimer_t* timer = NULL;
    while (timers->root) {
        // NOTE: root of minheap has min timeout.
        timer = TIMER_ENTRY(timers->root);
        if (timer->next_timeout > timeout) {
            break;
        }
        if (timer->repeat != INFINITE) {
            --timer->repeat;
        }
        if (timer->repeat == 0) {
            // NOTE: Just mark it as destroy and remove from heap.
            // Real deletion occurs after hloop_process_pendings.
            __htimer_del(timer);
        }
        else {
            // NOTE: calc next timeout, then re-insert heap.
            heap_dequeue(timers);
            if (timer->event_type == HEVENT_TYPE_TIMEOUT) {
                while (timer->next_timeout <= timeout) {
                    timer->next_timeout += (uint64_t)((htimeout_t*)timer)->timeout * 1000;
                }
            }
            else if (timer->event_type == HEVENT_TYPE_PERIOD) {
                hperiod_t* period = (hperiod_t*)timer;
                timer->next_timeout = (uint64_t)cron_next_timeout(period->minute, period->hour, period->day,
                        period->week, period->month) * 1000000;
            }
            heap_insert(timers, &timer->node);
        }
        EVENT_PENDING(timer);
        ++ntimers;
    }
    return ntimers;
}

static int hloop_process_timers(hloop_t* loop) {
    uint64_t now = hloop_now_us(loop);
    int ntimers = __hloop_process_timers(&loop->timers, loop->cur_hrtime);
    ntimers +=    __hloop_process_timers(&loop->realtimers, now);
    return ntimers;
}

static int hloop_process_ios(hloop_t* loop, int timeout) {
    // That is to call IO multiplexing function such as select, poll, epoll, etc.
    int nevents = iowatcher_poll_events(loop, timeout);
    if (nevents < 0) {
        hlogd("poll_events error=%d", -nevents);
    }
    return nevents < 0 ? 0 : nevents;
}

static int hloop_process_pendings(hloop_t* loop) {
    if (loop->npendings == 0) return 0;

    hevent_t* cur = NULL;
    hevent_t* next = NULL;
    int ncbs = 0;
    // NOTE: invoke event callback from high to low sorted by priority.
    for (int i = HEVENT_PRIORITY_SIZE-1; i >= 0; --i) {
        cur = loop->pendings[i];
        while (cur) {
            next = cur->pending_next;
            if (cur->pending) {
                if (cur->active && cur->cb) {
                    cur->cb(cur);
                    ++ncbs;
                }
                cur->pending = 0;
                // NOTE: Now we can safely delete event marked as destroy.
                if (cur->destroy) {
                    EVENT_DEL(cur);
                }
            }
            cur = next;
        }
        loop->pendings[i] = NULL;
    }
    loop->npendings = 0;
    return ncbs;
}

// hloop_process_ios -> hloop_process_timers -> hloop_process_idles -> hloop_process_pendings
static int hloop_process_events(hloop_t* loop) {
    // ios -> timers -> idles
    int nios, ntimers, nidles;
    nios = ntimers = nidles = 0;

    // calc blocktime
    int32_t blocktime_ms = HLOOP_MAX_BLOCK_TIME;
    if (loop->ntimers) {
        hloop_update_time(loop);
        int64_t blocktime_us = blocktime_ms * 1000;
        if (loop->timers.root) {
            int64_t min_timeout = TIMER_ENTRY(loop->timers.root)->next_timeout - loop->cur_hrtime;
            blocktime_us = MIN(blocktime_us, min_timeout);
        }
        if (loop->realtimers.root) {
            int64_t min_timeout = TIMER_ENTRY(loop->realtimers.root)->next_timeout - hloop_now_us(loop);
            blocktime_us = MIN(blocktime_us, min_timeout);
        }
        if (blocktime_us <= 0) goto process_timers;
        blocktime_ms = blocktime_us / 1000 + 1;
        blocktime_ms = MIN(blocktime_ms, HLOOP_MAX_BLOCK_TIME);
    }

    if (loop->nios) {
        nios = hloop_process_ios(loop, blocktime_ms);
    } else {
        hv_msleep(blocktime_ms);
    }
    hloop_update_time(loop);
    // wakeup by hloop_stop
    if (loop->status == HLOOP_STATUS_STOP) {
        return 0;
    }

process_timers:
    if (loop->ntimers) {
        ntimers = hloop_process_timers(loop);
    }

    int npendings = loop->npendings;
    if (npendings == 0) {
        if (loop->nidles) {
            nidles= hloop_process_idles(loop);
        }
    }
    int ncbs = hloop_process_pendings(loop);
    // printd("blocktime=%d nios=%d/%u ntimers=%d/%u nidles=%d/%u nactives=%d npendings=%d ncbs=%d\n",
    //         blocktime, nios, loop->nios, ntimers, loop->ntimers, nidles, loop->nidles,
    //         loop->nactives, npendings, ncbs);
    return ncbs;
}

static void hloop_stat_timer_cb(htimer_t* timer) {
    hloop_t* loop = timer->loop;
    // hlog_set_level(LOG_LEVEL_DEBUG);
    hlogd("[loop] pid=%ld tid=%ld uptime=%lluus cnt=%llu nactives=%u nios=%u ntimers=%u nidles=%u",
        loop->pid, loop->tid, loop->cur_hrtime - loop->start_hrtime, loop->loop_cnt,
        loop->nactives, loop->nios, loop->ntimers, loop->nidles);
}

static void eventfd_read_cb(hio_t* io, void* buf, int readbytes) {
    hloop_t* loop = io->loop;
    hevent_t* pev = NULL;
    hevent_t ev;
    uint64_t count = readbytes;
#if defined(OS_UNIX) && HAVE_EVENTFD
    assert(readbytes == sizeof(count));
    count = *(uint64_t*)buf;
#endif
    for (uint64_t i = 0; i < count; ++i) {
        hmutex_lock(&loop->custom_events_mutex);
        if (event_queue_empty(&loop->custom_events)) {
            goto unlock;
        }
        pev = event_queue_front(&loop->custom_events);
        if (pev == NULL) {
            goto unlock;
        }
        ev = *pev;
        event_queue_pop_front(&loop->custom_events);
        // NOTE: unlock before cb, avoid deadlock if hloop_post_event called in cb.
        hmutex_unlock(&loop->custom_events_mutex);
        if (ev.cb) {
            ev.cb(&ev);
        }
    }
    return;
unlock:
    hmutex_unlock(&loop->custom_events_mutex);
}

static int hloop_create_eventfds(hloop_t* loop) {
#if defined(OS_UNIX) && HAVE_EVENTFD
    int efd = eventfd(0, 0);
    if (efd < 0) {
        hloge("eventfd create failed!");
        return -1;
    }
    loop->eventfds[0] = loop->eventfds[1] = efd;
#elif defined(OS_UNIX) && HAVE_PIPE
    if (pipe(loop->eventfds) != 0) {
        hloge("pipe create failed!");
        return -1;
    }
#else
    if (Socketpair(AF_INET, SOCK_STREAM, 0, loop->eventfds) != 0) {
        hloge("socketpair create failed!");
        return -1;
    }
#endif
    hio_t* io = hread(loop, loop->eventfds[EVENTFDS_READ_INDEX], loop->readbuf.base, loop->readbuf.len, eventfd_read_cb);
    io->priority = HEVENT_HIGH_PRIORITY;
    ++loop->intern_nevents;
    return 0;
}

static void hloop_destroy_eventfds(hloop_t* loop) {
#if defined(OS_UNIX) && HAVE_EVENTFD
    // NOTE: eventfd has only one fd
    SAFE_CLOSE(loop->eventfds[0]);
#elif defined(OS_UNIX) && HAVE_PIPE
    SAFE_CLOSE(loop->eventfds[0]);
    SAFE_CLOSE(loop->eventfds[1]);
#else
    // NOTE: Avoid duplication closesocket in hio_cleanup
    // SAFE_CLOSESOCKET(loop->eventfds[EVENTFDS_READ_INDEX]);
    SAFE_CLOSESOCKET(loop->eventfds[EVENTFDS_WRITE_INDEX]);
#endif
    loop->eventfds[0] = loop->eventfds[1] = -1;
}

void hloop_post_event(hloop_t* loop, hevent_t* ev) {
    if (ev->loop == NULL) {
        ev->loop = loop;
    }
    if (ev->event_type == 0) {
        ev->event_type = HEVENT_TYPE_CUSTOM;
    }
    if (ev->event_id == 0) {
        ev->event_id = hloop_next_event_id();
    }

    int nwrite = 0;
    uint64_t count = 1;
    hmutex_lock(&loop->custom_events_mutex);
    if (loop->eventfds[EVENTFDS_WRITE_INDEX] == -1) {
        if (hloop_create_eventfds(loop) != 0) {
            goto unlock;
        }
    }
#if defined(OS_UNIX) && HAVE_EVENTFD
    nwrite = write(loop->eventfds[EVENTFDS_WRITE_INDEX], &count, sizeof(count));
#elif defined(OS_UNIX) && HAVE_PIPE
    nwrite = write(loop->eventfds[EVENTFDS_WRITE_INDEX], "e", 1);
#else
    nwrite =  send(loop->eventfds[EVENTFDS_WRITE_INDEX], "e", 1, 0);
#endif
    if (nwrite <= 0) {
        hloge("hloop_post_event failed!");
        goto unlock;
    }
    event_queue_push_back(&loop->custom_events, ev);
unlock:
    hmutex_unlock(&loop->custom_events_mutex);
}

static void hloop_init(hloop_t* loop) {
#ifdef OS_WIN
    WSAInit();
#endif
#ifdef SIGPIPE
    // NOTE: if not ignore SIGPIPE, write twice when peer close will lead to exit process by SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
#endif

    loop->status = HLOOP_STATUS_STOP;
    loop->pid = hv_getpid();
    loop->tid = hv_gettid();

    // idles
    list_init(&loop->idles);

    // timers
    heap_init(&loop->timers, timers_compare);
    heap_init(&loop->realtimers, timers_compare);

    // ios
    io_array_init(&loop->ios, IO_ARRAY_INIT_SIZE);

    // readbuf
    loop->readbuf.len = HLOOP_READ_BUFSIZE;
    HV_ALLOC(loop->readbuf.base, loop->readbuf.len);

    // iowatcher
    iowatcher_init(loop);

    // custom_events
    hmutex_init(&loop->custom_events_mutex);
    event_queue_init(&loop->custom_events, CUSTOM_EVENT_QUEUE_INIT_SIZE);
    // NOTE: hloop_create_eventfds when hloop_post_event or hloop_run
    loop->eventfds[0] = loop->eventfds[1] = -1;

    // NOTE: init start_time here, because htimer_add use it.
    loop->start_ms = gettimeofday_ms();
    loop->start_hrtime = loop->cur_hrtime = gethrtime_us();
}

static void hloop_cleanup(hloop_t* loop) {
    // pendings
    printd("cleanup pendings...\n");
    for (int i = 0; i < HEVENT_PRIORITY_SIZE; ++i) {
        loop->pendings[i] = NULL;
    }

    // ios
    printd("cleanup ios...\n");
    for (int i = 0; i < loop->ios.maxsize; ++i) {
        hio_t* io = loop->ios.ptr[i];
        if (io) {
            hio_free(io);
        }
    }
    io_array_cleanup(&loop->ios);

    // idles
    printd("cleanup idles...\n");
    struct list_node* node = loop->idles.next;
    hidle_t* idle;
    while (node != &loop->idles) {
        idle = IDLE_ENTRY(node);
        node = node->next;
        HV_FREE(idle);
    }
    list_init(&loop->idles);

    // timers
    printd("cleanup timers...\n");
    htimer_t* timer;
    while (loop->timers.root) {
        timer = TIMER_ENTRY(loop->timers.root);
        heap_dequeue(&loop->timers);
        HV_FREE(timer);
    }
    heap_init(&loop->timers, NULL);
    while (loop->realtimers.root) {
        timer = TIMER_ENTRY(loop->realtimers.root);
        heap_dequeue(&loop->realtimers);
        HV_FREE(timer);
    }
    heap_init(&loop->realtimers, NULL);

    // readbuf
    if (loop->readbuf.base && loop->readbuf.len) {
        HV_FREE(loop->readbuf.base);
        loop->readbuf.base = NULL;
        loop->readbuf.len = 0;
    }

    // iowatcher
    iowatcher_cleanup(loop);

    // custom_events
    hmutex_lock(&loop->custom_events_mutex);
    hloop_destroy_eventfds(loop);
    event_queue_cleanup(&loop->custom_events);
    hmutex_unlock(&loop->custom_events_mutex);
    hmutex_destroy(&loop->custom_events_mutex);
}

hloop_t* hloop_new(int flags) {
    hloop_t* loop;
    HV_ALLOC_SIZEOF(loop);
    hloop_init(loop);
    loop->flags |= flags;
    return loop;
}

void hloop_free(hloop_t** pp) {
    if (pp && *pp) {
        hloop_cleanup(*pp);
        HV_FREE(*pp);
        *pp = NULL;
    }
}

// while (loop->status) { hloop_process_events(loop); }
int hloop_run(hloop_t* loop) {
    if (loop == NULL) return -1;
    if (loop->status == HLOOP_STATUS_RUNNING) return -2;

    loop->status = HLOOP_STATUS_RUNNING;
    loop->pid = hv_getpid();
    loop->tid = hv_gettid();

    if (loop->intern_nevents == 0) {
        hmutex_lock(&loop->custom_events_mutex);
        if (loop->eventfds[EVENTFDS_WRITE_INDEX] == -1) {
            hloop_create_eventfds(loop);
        }
        hmutex_unlock(&loop->custom_events_mutex);

#ifdef DEBUG
        htimer_add(loop, hloop_stat_timer_cb, HLOOP_STAT_TIMEOUT, INFINITE);
        ++loop->intern_nevents;
#endif
    }

    while (loop->status != HLOOP_STATUS_STOP) {
        if (loop->status == HLOOP_STATUS_PAUSE) {
            hv_msleep(HLOOP_PAUSE_TIME);
            hloop_update_time(loop);
            continue;
        }
        ++loop->loop_cnt;
        if ((loop->flags & HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS) &&
            loop->nactives <= loop->intern_nevents) {
            break;
        }
        hloop_process_events(loop);
        if (loop->flags & HLOOP_FLAG_RUN_ONCE) {
            break;
        }
    }

    loop->status = HLOOP_STATUS_STOP;
    loop->end_hrtime = gethrtime_us();

    if (loop->flags & HLOOP_FLAG_AUTO_FREE) {
        hloop_cleanup(loop);
        HV_FREE(loop);
    }
    return 0;
}

int hloop_wakeup(hloop_t* loop) {
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    hloop_post_event(loop, &ev);
    return 0;
}

int hloop_stop(hloop_t* loop) {
    if (hv_gettid() != loop->tid) {
        hloop_wakeup(loop);
    }
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

hloop_status_e hloop_status(hloop_t* loop) {
    return loop->status;
}

void hloop_update_time(hloop_t* loop) {
    loop->cur_hrtime = gethrtime_us();
    if (hloop_now(loop) != time(NULL)) {
        // systemtime changed, we adjust start_ms
        loop->start_ms = gettimeofday_ms() - (loop->cur_hrtime - loop->start_hrtime) / 1000;
    }
}

uint64_t hloop_now(hloop_t* loop) {
    return loop->start_ms / 1000 + (loop->cur_hrtime - loop->start_hrtime) / 1000000;
}

uint64_t hloop_now_ms(hloop_t* loop) {
    return loop->start_ms + (loop->cur_hrtime - loop->start_hrtime) / 1000;
}

uint64_t hloop_now_us(hloop_t* loop) {
    return loop->start_ms * 1000 + (loop->cur_hrtime - loop->start_hrtime);
}

uint64_t hloop_now_hrtime(hloop_t* loop) {
    return loop->cur_hrtime;
}

uint64_t hio_last_read_time(hio_t* io) {
    hloop_t* loop = io->loop;
    return loop->start_ms + (io->last_read_hrtime - loop->start_hrtime) / 1000;
}

uint64_t hio_last_write_time(hio_t* io) {
    hloop_t* loop = io->loop;
    return loop->start_ms + (io->last_write_hrtime - loop->start_hrtime) / 1000;
}

long hloop_pid(hloop_t* loop) {
    return loop->pid;
}

long hloop_tid(hloop_t* loop) {
    return loop->tid;
}

uint64_t hloop_count(hloop_t* loop) {
    return loop->loop_cnt;
}

uint32_t hloop_nios(hloop_t* loop) {
    return loop->nios;
}

uint32_t hloop_ntimers(hloop_t* loop) {
    return loop->ntimers;
}

uint32_t hloop_nidles(hloop_t* loop) {
    return loop->nidles;
}

uint32_t hloop_nactives(hloop_t* loop) {
    return loop->nactives;
}

void  hloop_set_userdata(hloop_t* loop, void* userdata) {
    loop->userdata = userdata;
}

void* hloop_userdata(hloop_t* loop) {
    return loop->userdata;
}

hidle_t* hidle_add(hloop_t* loop, hidle_cb cb, uint32_t repeat) {
    hidle_t* idle;
    HV_ALLOC_SIZEOF(idle);
    idle->event_type = HEVENT_TYPE_IDLE;
    idle->priority = HEVENT_LOWEST_PRIORITY;
    idle->repeat = repeat;
    list_add(&idle->node, &loop->idles);
    EVENT_ADD(loop, idle, cb);
    loop->nidles++;
    return idle;
}

static void __hidle_del(hidle_t* idle) {
    if (idle->destroy) return;
    idle->destroy = 1;
    list_del(&idle->node);
    idle->loop->nidles--;
}

void hidle_del(hidle_t* idle) {
    if (!idle->active) return;
    __hidle_del(idle);
    EVENT_DEL(idle);
}

htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint32_t timeout_ms, uint32_t repeat) {
    if (timeout_ms == 0)   return NULL;
    htimeout_t* timer;
    HV_ALLOC_SIZEOF(timer);
    timer->event_type = HEVENT_TYPE_TIMEOUT;
    timer->priority = HEVENT_HIGHEST_PRIORITY;
    timer->repeat = repeat;
    timer->timeout = timeout_ms;
    hloop_update_time(loop);
    timer->next_timeout = loop->cur_hrtime + (uint64_t)timeout_ms * 1000;
    // NOTE: Limit granularity to 100ms
    if (timeout_ms >= 1000 && timeout_ms % 100 == 0) {
        timer->next_timeout = timer->next_timeout / 100000 * 100000;
    }
    heap_insert(&loop->timers, &timer->node);
    EVENT_ADD(loop, timer, cb);
    loop->ntimers++;
    return (htimer_t*)timer;
}

void htimer_reset(htimer_t* timer, uint32_t timeout_ms) {
    if (timer->event_type != HEVENT_TYPE_TIMEOUT) {
        return;
    }
    hloop_t* loop = timer->loop;
    htimeout_t* timeout = (htimeout_t*)timer;
    if (timer->destroy) {
        loop->ntimers++;
    } else {
        heap_remove(&loop->timers, &timer->node);
    }
    if (timer->repeat == 0) {
        timer->repeat = 1;
    }
    if (timeout_ms > 0) {
        timeout->timeout = timeout_ms;
    }
    timer->next_timeout = loop->cur_hrtime + (uint64_t)timeout->timeout * 1000;
    // NOTE: Limit granularity to 100ms
    if (timeout->timeout >= 1000 && timeout->timeout % 100 == 0) {
        timer->next_timeout = timer->next_timeout / 100000 * 100000;
    }
    heap_insert(&loop->timers, &timer->node);
    EVENT_RESET(timer);
}

htimer_t* htimer_add_period(hloop_t* loop, htimer_cb cb,
                int8_t minute,  int8_t hour, int8_t day,
                int8_t week, int8_t month, uint32_t repeat) {
    if (minute > 59 || hour > 23 || day > 31 || week > 6 || month > 12) {
        return NULL;
    }
    hperiod_t* timer;
    HV_ALLOC_SIZEOF(timer);
    timer->event_type = HEVENT_TYPE_PERIOD;
    timer->priority = HEVENT_HIGH_PRIORITY;
    timer->repeat = repeat;
    timer->minute = minute;
    timer->hour   = hour;
    timer->day    = day;
    timer->month  = month;
    timer->week   = week;
    timer->next_timeout = (uint64_t)cron_next_timeout(minute, hour, day, week, month) * 1000000;
    heap_insert(&loop->realtimers, &timer->node);
    EVENT_ADD(loop, timer, cb);
    loop->ntimers++;
    return (htimer_t*)timer;
}

static void __htimer_del(htimer_t* timer) {
    if (timer->destroy) return;
    if (timer->event_type == HEVENT_TYPE_TIMEOUT) {
        heap_remove(&timer->loop->timers, &timer->node);
    } else if (timer->event_type == HEVENT_TYPE_PERIOD) {
        heap_remove(&timer->loop->realtimers, &timer->node);
    }
    timer->loop->ntimers--;
    timer->destroy = 1;
}

void htimer_del(htimer_t* timer) {
    if (!timer->active) return;
    __htimer_del(timer);
    EVENT_DEL(timer);
}

const char* hio_engine() {
#ifdef EVENT_SELECT
    return  "select";
#elif defined(EVENT_POLL)
    return  "poll";
#elif defined(EVENT_EPOLL)
    return  "epoll";
#elif defined(EVENT_KQUEUE)
    return  "kqueue";
#elif defined(EVENT_IOCP)
    return  "iocp";
#elif defined(EVENT_PORT)
    return  "evport";
#else
    return  "noevent";
#endif
}

hio_t* hio_get(hloop_t* loop, int fd) {
    if (fd >= loop->ios.maxsize) {
        int newsize = ceil2e(fd);
        io_array_resize(&loop->ios, newsize > fd ? newsize : 2*fd);
    }

    hio_t* io = loop->ios.ptr[fd];
    if (io == NULL) {
        HV_ALLOC_SIZEOF(io);
        hio_init(io);
        io->event_type = HEVENT_TYPE_IO;
        io->loop = loop;
        io->fd = fd;
        loop->ios.ptr[fd] = io;
    }

    if (!io->ready) {
        hio_ready(io);
    }

    return io;
}

void hio_detach(hio_t* io) {
    hloop_t* loop = io->loop;
    int fd = io->fd;
    assert(loop != NULL && fd < loop->ios.maxsize);
    loop->ios.ptr[fd] = NULL;
}

void hio_attach(hloop_t* loop, hio_t* io) {
    int fd = io->fd;
    if (fd >= loop->ios.maxsize) {
        int newsize = ceil2e(fd);
        io_array_resize(&loop->ios, newsize > fd ? newsize : 2*fd);
    }

    // NOTE: hio was not freed for reused when closed, but attached hio can't be reused,
    // so we need to free it if fd exists to avoid memory leak.
    hio_t* preio = loop->ios.ptr[fd];
    if (preio != NULL && preio != io) {
        hio_free(preio);
    }

    io->loop = loop;
    // NOTE: use new_loop readbuf
    io->readbuf.base = loop->readbuf.base;
    io->readbuf.len = loop->readbuf.len;
    loop->ios.ptr[fd] = io;
}

bool hio_exists(hloop_t* loop, int fd) {
    if (fd >= loop->ios.maxsize) {
        return false;
    }
    return loop->ios.ptr[fd] != NULL;
}

int hio_add(hio_t* io, hio_cb cb, int events) {
    printd("hio_add fd=%d io->events=%d events=%d\n", io->fd, io->events, events);
#ifdef OS_WIN
    // Windows iowatcher not work on stdio
    if (io->fd < 3) return -1;
#endif
    hloop_t* loop = io->loop;
    if (!io->active) {
        EVENT_ADD(loop, io, cb);
        loop->nios++;
    }

    if (!io->ready) {
        hio_ready(io);
    }

    if (cb) {
        io->cb = (hevent_cb)cb;
    }

    if (!(io->events & events)) {
        iowatcher_add_event(loop, io->fd, events);
        io->events |= events;
    }
    return 0;
}

int hio_del(hio_t* io, int events) {
    printd("hio_del fd=%d io->events=%d events=%d\n", io->fd, io->events, events);
#ifdef OS_WIN
    // Windows iowatcher not work on stdio
    if (io->fd < 3) return -1;
#endif
    if (!io->active) return -1;

    if (io->events & events) {
        iowatcher_del_event(io->loop, io->fd, events);
        io->events &= ~events;
    }
    if (io->events == 0) {
        io->loop->nios--;
        // NOTE: not EVENT_DEL, avoid free
        EVENT_INACTIVE(io);
    }
    return 0;
}

static void hio_close_event_cb(hevent_t* ev) {
    hio_t* io = (hio_t*)ev->userdata;
    uint32_t id = (uintptr_t)ev->privdata;
    if (io->id != id) return;
    hio_close(io);
}

int hio_close_async(hio_t* io) {
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.cb = hio_close_event_cb;
    ev.userdata = io;
    ev.privdata = (void*)(uintptr_t)io->id;
    hloop_post_event(io->loop, &ev);
    return 0;
}

//------------------high-level apis-------------------------------------------
hio_t* hread(hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb) {
    hio_t* io = hio_get(loop, fd);
    assert(io != NULL);
    if (buf && len) {
        io->readbuf.base = (char*)buf;
        io->readbuf.len = len;
    }
    if (read_cb) {
        io->read_cb = read_cb;
    }
    hio_read(io);
    return io;
}

hio_t* hwrite(hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb) {
    hio_t* io = hio_get(loop, fd);
    assert(io != NULL);
    if (write_cb) {
        io->write_cb = write_cb;
    }
    hio_write(io, buf, len);
    return io;
}

hio_t* haccept(hloop_t* loop, int listenfd, haccept_cb accept_cb) {
    hio_t* io = hio_get(loop, listenfd);
    assert(io != NULL);
    if (accept_cb) {
        io->accept_cb = accept_cb;
    }
    if (hio_accept(io) != 0) return NULL;
    return io;
}

hio_t* hconnect (hloop_t* loop, int connfd, hconnect_cb connect_cb) {
    hio_t* io = hio_get(loop, connfd);
    assert(io != NULL);
    if (connect_cb) {
        io->connect_cb = connect_cb;
    }
    if (hio_connect(io) != 0) return NULL;
    return io;
}

void hclose (hloop_t* loop, int fd) {
    hio_t* io = hio_get(loop, fd);
    assert(io != NULL);
    hio_close(io);
}

hio_t* hrecv (hloop_t* loop, int connfd, void* buf, size_t len, hread_cb read_cb) {
    //hio_t* io = hio_get(loop, connfd);
    //assert(io != NULL);
    //io->recv = 1;
    //if (io->io_type != HIO_TYPE_SSL) {
        //io->io_type = HIO_TYPE_TCP;
    //}
    return hread(loop, connfd, buf, len, read_cb);
}

hio_t* hsend (hloop_t* loop, int connfd, const void* buf, size_t len, hwrite_cb write_cb) {
    //hio_t* io = hio_get(loop, connfd);
    //assert(io != NULL);
    //io->send = 1;
    //if (io->io_type != HIO_TYPE_SSL) {
        //io->io_type = HIO_TYPE_TCP;
    //}
    return hwrite(loop, connfd, buf, len, write_cb);
}

hio_t* hrecvfrom (hloop_t* loop, int sockfd, void* buf, size_t len, hread_cb read_cb) {
    //hio_t* io = hio_get(loop, sockfd);
    //assert(io != NULL);
    //io->recvfrom = 1;
    //io->io_type = HIO_TYPE_UDP;
    return hread(loop, sockfd, buf, len, read_cb);
}

hio_t* hsendto (hloop_t* loop, int sockfd, const void* buf, size_t len, hwrite_cb write_cb) {
    //hio_t* io = hio_get(loop, sockfd);
    //assert(io != NULL);
    //io->sendto = 1;
    //io->io_type = HIO_TYPE_UDP;
    return hwrite(loop, sockfd, buf, len, write_cb);
}

//-----------------top-level apis---------------------------------------------
hio_t* hio_create_socket(hloop_t* loop, const char* host, int port, hio_type_e type, hio_side_e side) {
    int sock_type = type & HIO_TYPE_SOCK_STREAM ? SOCK_STREAM :
                    type & HIO_TYPE_SOCK_DGRAM  ? SOCK_DGRAM :
                    type & HIO_TYPE_SOCK_RAW    ? SOCK_RAW : -1;
    if (sock_type == -1) return NULL;
    sockaddr_u addr;
    memset(&addr, 0, sizeof(addr));
    int ret = -1;
#ifdef ENABLE_UDS
    if (port < 0) {
        sockaddr_set_path(&addr, host);
        ret = 0;
    }
#endif
    if (port >= 0) {
        ret = sockaddr_set_ipport(&addr, host, port);
    }
    if (ret != 0) {
        // fprintf(stderr, "unknown host: %s\n", host);
        return NULL;
    }
    int sockfd = socket(addr.sa.sa_family, sock_type, 0);
    if (sockfd < 0) {
        perror("socket");
        return NULL;
    }
    hio_t* io = NULL;
    if (side == HIO_SERVER_SIDE) {
#ifdef SO_REUSEADDR
        // NOTE: SO_REUSEADDR allow to reuse sockaddr of TIME_WAIT status
        int reuseaddr = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(int)) < 0) {
            perror("setsockopt");
            closesocket(sockfd);
            return NULL;
        }
#endif
        if (bind(sockfd, &addr.sa, sockaddr_len(&addr)) < 0) {
            perror("bind");
            closesocket(sockfd);
            return NULL;
        }
        if (sock_type == SOCK_STREAM) {
            if (listen(sockfd, SOMAXCONN) < 0) {
                perror("listen");
                closesocket(sockfd);
                return NULL;
            }
        }
    }
    io = hio_get(loop, sockfd);
    assert(io != NULL);
    io->io_type = type;
    if (side == HIO_SERVER_SIDE) {
        hio_set_localaddr(io, &addr.sa, sockaddr_len(&addr));
        io->priority = HEVENT_HIGH_PRIORITY;
    } else {
        hio_set_peeraddr(io, &addr.sa, sockaddr_len(&addr));
    }
    return io;
}

hio_t* hloop_create_tcp_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb) {
    hio_t* io = hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_SERVER_SIDE);
    if (io == NULL) return NULL;
    hio_setcb_accept(io, accept_cb);
    if (hio_accept(io) != 0) return NULL;
    return io;
}

hio_t* hloop_create_tcp_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb, hclose_cb close_cb) {
    hio_t* io = hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) return NULL;
    hio_setcb_connect(io, connect_cb);
    hio_setcb_close(io, close_cb);
    if (hio_connect(io) != 0) return NULL;
    return io;
}

hio_t* hloop_create_ssl_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb) {
    hio_t* io = hio_create_socket(loop, host, port, HIO_TYPE_SSL, HIO_SERVER_SIDE);
    if (io == NULL) return NULL;
    hio_setcb_accept(io, accept_cb);
    if (hio_accept(io) != 0) return NULL;
    return io;
}

hio_t* hloop_create_ssl_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb, hclose_cb close_cb) {
    hio_t* io = hio_create_socket(loop, host, port, HIO_TYPE_SSL, HIO_CLIENT_SIDE);
    if (io == NULL) return NULL;
    hio_setcb_connect(io, connect_cb);
    hio_setcb_close(io, close_cb);
    if (hio_connect(io) != 0) return NULL;
    return io;
}

hio_t* hloop_create_udp_server(hloop_t* loop, const char* host, int port) {
    return hio_create_socket(loop, host, port, HIO_TYPE_UDP, HIO_SERVER_SIDE);
}

hio_t* hloop_create_udp_client(hloop_t* loop, const char* host, int port) {
    return hio_create_socket(loop, host, port, HIO_TYPE_UDP, HIO_CLIENT_SIDE);
}
