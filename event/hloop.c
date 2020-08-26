#include "hloop.h"
#include "hevent.h"
#include "iowatcher.h"

#include "hdef.h"
#include "hbase.h"
#include "hlog.h"
#include "hmath.h"
#include "htime.h"
#include "hsocket.h"

#define PAUSE_TIME              10          // ms
#define MAX_BLOCK_TIME          1000        // ms

#define IO_ARRAY_INIT_SIZE              1024
#define CUSTOM_EVENT_QUEUE_INIT_SIZE    16

/*
 * hio lifeline:
 * hio_get => HV_ALLOC_SIZEOF(io) => hio_init =>
 * hio_ready => hio_add => hio_del => hio_done =>
 * hio_free => HV_FREE(io)
 */
static void hio_init(hio_t* io);
static void hio_ready(hio_t* io);
static void hio_done(hio_t* io);
static void hio_free(hio_t* io);

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
            __hidle_del(idle);
        }
        EVENT_PENDING(idle);
        ++nidles;
    }
    return nidles;
}

static int hloop_process_timers(hloop_t* loop) {
    int ntimers = 0;
    htimer_t* timer = NULL;
    uint64_t now_hrtime = hloop_now_hrtime(loop);
    while (loop->timers.root) {
        timer = TIMER_ENTRY(loop->timers.root);
        if (timer->next_timeout > now_hrtime) {
            break;
        }
        if (timer->repeat != INFINITE) {
            --timer->repeat;
        }
        if (timer->repeat == 0) {
            __htimer_del(timer);
        }
        else {
            heap_dequeue(&loop->timers);
            if (timer->event_type == HEVENT_TYPE_TIMEOUT) {
                while (timer->next_timeout <= now_hrtime) {
                    timer->next_timeout += ((htimeout_t*)timer)->timeout * 1000;
                }
            }
            else if (timer->event_type == HEVENT_TYPE_PERIOD) {
                hperiod_t* period = (hperiod_t*)timer;
                timer->next_timeout = cron_next_timeout(period->minute, period->hour, period->day,
                        period->week, period->month) * 1000000;
            }
            heap_insert(&loop->timers, &timer->node);
        }
        EVENT_PENDING(timer);
        ++ntimers;
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

    hevent_t* cur = NULL;
    hevent_t* next = NULL;
    int ncbs = 0;
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

static int hloop_process_events(hloop_t* loop) {
    // ios -> timers -> idles
    int nios, ntimers, nidles;
    nios = ntimers = nidles = 0;

    // calc blocktime
    int32_t blocktime = MAX_BLOCK_TIME;
    if (loop->timers.root) {
        hloop_update_time(loop);
        uint64_t next_min_timeout = TIMER_ENTRY(loop->timers.root)->next_timeout;
        int64_t blocktime_us = next_min_timeout - hloop_now_hrtime(loop);
        if (blocktime_us <= 0) goto process_timers;
        blocktime = blocktime_us / 1000;
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

    int npendings = loop->npendings;
    if (npendings == 0) {
        if (loop->nidles) {
            nidles= hloop_process_idles(loop);
        }
    }
    int ncbs = hloop_process_pendings(loop);
    //printd("blocktime=%d nios=%d/%u ntimers=%d/%u nidles=%d/%u nactives=%d npendings=%d ncbs=%d\n",
            //blocktime, nios, loop->nios, ntimers, loop->ntimers, nidles, loop->nidles,
            //loop->nactives, npendings, ncbs);
    return ncbs;
}

static void hloop_init(hloop_t* loop) {
    loop->status = HLOOP_STATUS_STOP;

    // idles
    list_init(&loop->idles);

    // timers
    heap_init(&loop->timers, timers_compare);

    // ios: init when hio_get
    // io_array_init(&loop->ios, IO_ARRAY_INIT_SIZE);

    // readbuf: alloc when hio_set_readbuf
    // loop->readbuf.len = HLOOP_READ_BUFSIZE;
    // HV_ALLOC(loop->readbuf.base, loop->readbuf.len);

    // iowatcher: init when iowatcher_add_event
    // iowatcher_init(loop);

    // custom_events: init when hloop_post_event
    // event_queue_init(&loop->custom_events, 4);
    loop->sockpair[0] = loop->sockpair[1] = -1;
    hmutex_init(&loop->custom_events_mutex);

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

    // ios
    printd("cleanup ios...\n");
    for (int i = 0; i < loop->ios.maxsize; ++i) {
        hio_t* io = loop->ios.ptr[i];
        if (io) {
            hio_free(io);
        }
    }
    io_array_cleanup(&loop->ios);

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
    if (loop->sockpair[0] != -1 && loop->sockpair[1] != -1) {
        closesocket(loop->sockpair[0]);
        closesocket(loop->sockpair[1]);
        loop->sockpair[0] = loop->sockpair[1] = -1;
    }
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

int hloop_run(hloop_t* loop) {
    loop->status = HLOOP_STATUS_RUNNING;
    while (loop->status != HLOOP_STATUS_STOP) {
        if (loop->status == HLOOP_STATUS_PAUSE) {
            msleep(PAUSE_TIME);
            hloop_update_time(loop);
            continue;
        }
        ++loop->loop_cnt;
        if (loop->nactives == 0 && loop->flags & HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS) {
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

void hloop_update_time(hloop_t* loop) {
    loop->cur_hrtime = gethrtime_us();
    if (ABS((int64_t)hloop_now(loop) - (int64_t)time(NULL)) > 1) {
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

uint64_t hloop_now_hrtime(hloop_t* loop) {
    return loop->start_ms * 1000 + (loop->cur_hrtime - loop->start_hrtime);
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
    EVENT_DEL(idle);
    __hidle_del(idle);
}

htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint32_t timeout, uint32_t repeat) {
    if (timeout == 0)   return NULL;
    htimeout_t* timer;
    HV_ALLOC_SIZEOF(timer);
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

void htimer_reset(htimer_t* timer) {
    if (timer->event_type != HEVENT_TYPE_TIMEOUT) {
        return;
    }
    hloop_t* loop = timer->loop;
    htimeout_t* timeout = (htimeout_t*)timer;
    if (timer->pending) {
        if (timer->repeat == 0) {
            timer->repeat = 1;
        }
    }
    else {
        heap_remove(&loop->timers, &timer->node);
    }
    timer->next_timeout = hloop_now_hrtime(loop) + timeout->timeout*1000;
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
    timer->next_timeout = cron_next_timeout(minute, hour, day, week, month) * 1000000;
    heap_insert(&loop->timers, &timer->node);
    EVENT_ADD(loop, timer, cb);
    loop->ntimers++;
    return (htimer_t*)timer;
}

static void __htimer_del(htimer_t* timer) {
    if (timer->destroy) return;
    heap_remove(&timer->loop->timers, &timer->node);
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

static void fill_io_type(hio_t* io) {
    int type = 0;
    socklen_t optlen = sizeof(int);
    int ret = getsockopt(io->fd, SOL_SOCKET, SO_TYPE, (char*)&type, &optlen);
    printd("getsockopt SO_TYPE fd=%d ret=%d type=%d errno=%d\n", io->fd, ret, type, socket_errno());
    if (ret == 0) {
        switch (type) {
        case SOCK_STREAM:   io->io_type = HIO_TYPE_TCP; break;
        case SOCK_DGRAM:    io->io_type = HIO_TYPE_UDP; break;
        case SOCK_RAW:      io->io_type = HIO_TYPE_IP;  break;
        default: io->io_type = HIO_TYPE_SOCKET;         break;
        }
    }
    else if (socket_errno() == ENOTSOCK) {
        switch (io->fd) {
        case 0: io->io_type = HIO_TYPE_STDIN;   break;
        case 1: io->io_type = HIO_TYPE_STDOUT;  break;
        case 2: io->io_type = HIO_TYPE_STDERR;  break;
        default: io->io_type = HIO_TYPE_FILE;   break;
        }
    }
}

static void hio_socket_init(hio_t* io) {
    // nonblocking
    nonblocking(io->fd);
    // fill io->localaddr io->peeraddr
    if (io->localaddr == NULL) {
        HV_ALLOC(io->localaddr, sizeof(sockaddr_u));
    }
    if (io->peeraddr == NULL) {
        HV_ALLOC(io->peeraddr, sizeof(sockaddr_u));
    }
    socklen_t addrlen = sizeof(sockaddr_u);
    int ret = getsockname(io->fd, io->localaddr, &addrlen);
    printd("getsockname fd=%d ret=%d errno=%d\n", io->fd, ret, socket_errno());
    // NOTE:
    // tcp_server peeraddr set by accept
    // udp_server peeraddr set by recvfrom
    // tcp_client/udp_client peeraddr set by hio_setpeeraddr
    if (io->io_type == HIO_TYPE_TCP || io->io_type == HIO_TYPE_SSL) {
        // tcp acceptfd
        addrlen = sizeof(sockaddr_u);
        ret = getpeername(io->fd, io->peeraddr, &addrlen);
        printd("getpeername fd=%d ret=%d errno=%d\n", io->fd, ret, socket_errno());
    }
}

void hio_init(hio_t* io) {
    // alloc localaddr,peeraddr when hio_socket_init
    /*
    if (io->localaddr == NULL) {
        HV_ALLOC(io->localaddr, sizeof(sockaddr_u));
    }
    if (io->peeraddr == NULL) {
        HV_ALLOC(io->peeraddr, sizeof(sockaddr_u));
    }
    */

    // write_queue init when hwrite try_write failed
    // write_queue_init(&io->write_queue, 4);
}

void hio_ready(hio_t* io) {
    if (io->ready) return;
    // flags
    io->ready = 1;
    io->closed = 0;
    io->accept = io->connect = io->connectex = 0;
    io->recv = io->send = 0;
    io->recvfrom = io->sendto = 0;
    io->close = 0;
    // public:
    io->io_type = HIO_TYPE_UNKNOWN;
    io->error = 0;
    io->events = io->revents = 0;
    // callbacks
    io->read_cb = NULL;
    io->write_cb = NULL;
    io->close_cb = 0;
    io->accept_cb = 0;
    io->connect_cb = 0;
    // timers
    io->connect_timeout = 0;
    io->connect_timer = NULL;
    io->close_timeout = 0;
    io->close_timer = NULL;
    io->keepalive_timeout = 0;
    io->keepalive_timer = NULL;
    io->heartbeat_interval = 0;
    io->heartbeat_fn = NULL;
    io->heartbeat_timer = NULL;
    // private:
    io->event_index[0] = io->event_index[1] = -1;
    io->hovlp = NULL;
    io->ssl = NULL;

    // io_type
    fill_io_type(io);
    if (io->io_type & HIO_TYPE_SOCKET) {
        hio_socket_init(io);
    }
}

void hio_done(hio_t* io) {
    if (!io->ready) return;
    io->ready = 0;

    offset_buf_t* pbuf = NULL;
    while (!write_queue_empty(&io->write_queue)) {
        pbuf = write_queue_front(&io->write_queue);
        HV_FREE(pbuf->base);
        write_queue_pop_front(&io->write_queue);
    }
    write_queue_cleanup(&io->write_queue);
}

void hio_free(hio_t* io) {
    if (io == NULL) return;
    // NOTE: call hio_done to cleanup write_queue
    hio_done(io);
    hio_close(io);
    HV_FREE(io->localaddr);
    HV_FREE(io->peeraddr);
    HV_FREE(io);
}

hio_t* hio_get(hloop_t* loop, int fd) {
    if (loop->ios.maxsize == 0) {
        io_array_init(&loop->ios, IO_ARRAY_INIT_SIZE);
    }

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

int hio_add(hio_t* io, hio_cb cb, int events) {
    printd("hio_add fd=%d events=%d\n", io->fd, events);
#ifdef OS_WIN
    // Windows iowatcher not work on stdio
    if (io->fd < 3) return 0;
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

    iowatcher_add_event(loop, io->fd, events);
    io->events |= events;
    return 0;
}

int hio_del(hio_t* io, int events) {
    printd("hio_del fd=%d io->events=%d events=%d\n", io->fd, io->events, events);
#ifdef OS_WIN
    // Windows iowatcher not work on stdio
    if (io->fd < 3) return 0;
#endif
    if (!io->active || !io->ready) return 0;
    iowatcher_del_event(io->loop, io->fd, events);
    io->events &= ~events;
    if (io->events == 0) {
        io->loop->nios--;
        // NOTE: not EVENT_DEL, avoid free
        EVENT_INACTIVE(io);
        hio_done(io);
    }
    return 0;
}

hio_t* hread(hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb) {
    hio_t* io = hio_get(loop, fd);
    if (io == NULL) return NULL;
    io->readbuf.base = (char*)buf;
    io->readbuf.len = len;
    if (read_cb) {
        io->read_cb = read_cb;
    }
    hio_read(io);
    return io;
}

hio_t* hwrite(hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb) {
    hio_t* io = hio_get(loop, fd);
    if (io == NULL) return NULL;
    if (write_cb) {
        io->write_cb = write_cb;
    }
    hio_write(io, buf, len);
    return io;
}

hio_t* haccept(hloop_t* loop, int listenfd, haccept_cb accept_cb) {
    hio_t* io = hio_get(loop, listenfd);
    if (io == NULL) return NULL;
    io->accept = 1;
    if (accept_cb) {
        io->accept_cb = accept_cb;
    }
    hio_accept(io);
    return io;
}

hio_t* hconnect (hloop_t* loop, int connfd, hconnect_cb connect_cb) {
    hio_t* io = hio_get(loop, connfd);
    if (io == NULL) return NULL;
    io->connect = 1;
    if (connect_cb) {
        io->connect_cb = connect_cb;
    }
    hio_connect(io);
    return io;
}

void hclose (hloop_t* loop, int fd) {
    hio_t* io = hio_get(loop, fd);
    if (io == NULL) return;
    hio_close(io);
}

hio_t* hrecv (hloop_t* loop, int connfd, void* buf, size_t len, hread_cb read_cb) {
    //hio_t* io = hio_get(loop, connfd);
    //if (io == NULL) return NULL;
    //io->recv = 1;
    //if (io->io_type != HIO_TYPE_SSL) {
        //io->io_type = HIO_TYPE_TCP;
    //}
    return hread(loop, connfd, buf, len, read_cb);
}

hio_t* hsend (hloop_t* loop, int connfd, const void* buf, size_t len, hwrite_cb write_cb) {
    //hio_t* io = hio_get(loop, connfd);
    //if (io == NULL) return NULL;
    //io->send = 1;
    //if (io->io_type != HIO_TYPE_SSL) {
        //io->io_type = HIO_TYPE_TCP;
    //}
    return hwrite(loop, connfd, buf, len, write_cb);
}

hio_t* hrecvfrom (hloop_t* loop, int sockfd, void* buf, size_t len, hread_cb read_cb) {
    //hio_t* io = hio_get(loop, sockfd);
    //if (io == NULL) return NULL;
    //io->recvfrom = 1;
    //io->io_type = HIO_TYPE_UDP;
    return hread(loop, sockfd, buf, len, read_cb);
}

hio_t* hsendto (hloop_t* loop, int sockfd, const void* buf, size_t len, hwrite_cb write_cb) {
    //hio_t* io = hio_get(loop, sockfd);
    //if (io == NULL) return NULL;
    //io->sendto = 1;
    //io->io_type = HIO_TYPE_UDP;
    return hwrite(loop, sockfd, buf, len, write_cb);
}

hio_t* hloop_create_tcp_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb) {
    int listenfd = Listen(port, host);
    if (listenfd < 0) {
        return NULL;
    }
    hio_t* io = haccept(loop, listenfd, accept_cb);
    if (io == NULL) {
        closesocket(listenfd);
    }
    return io;
}

hio_t* hloop_create_tcp_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb) {
    sockaddr_u peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    int ret = sockaddr_set_ipport(&peeraddr, host, port);
    if (ret != 0) {
        //printf("unknown host: %s\n", host);
        return NULL;
    }
    int connfd = socket(peeraddr.sa.sa_family, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        return NULL;
    }

    hio_t* io = hio_get(loop, connfd);
    if (io == NULL) return NULL;
    hio_set_peeraddr(io, &peeraddr.sa, sockaddr_len(&peeraddr));
    hconnect(loop, connfd, connect_cb);
    return io;
}

// @server: socket -> bind -> hrecvfrom
hio_t* hloop_create_udp_server(hloop_t* loop, const char* host, int port) {
    int bindfd = Bind(port, host, SOCK_DGRAM);
    if (bindfd < 0) {
        return NULL;
    }
    return hio_get(loop, bindfd);
}

// @client: Resolver -> socket -> hio_get -> hio_set_peeraddr
hio_t* hloop_create_udp_client(hloop_t* loop, const char* host, int port) {
    sockaddr_u peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    int ret = sockaddr_set_ipport(&peeraddr, host, port);
    if (ret != 0) {
        //printf("unknown host: %s\n", host);
        return NULL;
    }

    int sockfd = socket(peeraddr.sa.sa_family, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return NULL;
    }

    hio_t* io = hio_get(loop, sockfd);
    if (io == NULL) return NULL;
    hio_set_peeraddr(io, &peeraddr.sa, sockaddr_len(&peeraddr));
    return io;
}

static void sockpair_read_cb(hio_t* io, void* buf, int readbytes) {
    hloop_t* loop = io->loop;
    hevent_t* pev = NULL;
    hevent_t ev;
    for (int i = 0; i < readbytes; ++i) {
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

void hloop_post_event(hloop_t* loop, hevent_t* ev) {
    char buf = '1';
    hmutex_lock(&loop->custom_events_mutex);
    if (loop->sockpair[0] <= 0 && loop->sockpair[1] <= 0) {
        if (Socketpair(AF_INET, SOCK_STREAM, 0, loop->sockpair) != 0) {
            hloge("socketpair error");
            goto unlock;
        }
        hread(loop, loop->sockpair[1], loop->readbuf.base, loop->readbuf.len, sockpair_read_cb);
    }
    if (loop->custom_events.maxsize == 0) {
        event_queue_init(&loop->custom_events, CUSTOM_EVENT_QUEUE_INIT_SIZE);
    }
    if (ev->loop == NULL) {
        ev->loop = loop;
    }
    if (ev->event_type == 0) {
        ev->event_type = HEVENT_TYPE_CUSTOM;
    }
    if (ev->event_id == 0) {
        ev->event_id = ++loop->event_counter;
    }
    event_queue_push_back(&loop->custom_events, ev);
    hwrite(loop, loop->sockpair[0], &buf, 1, NULL);
unlock:
    hmutex_unlock(&loop->custom_events_mutex);
}
