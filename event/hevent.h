#ifndef HV_EVENT_H_
#define HV_EVENT_H_

#include "hloop.h"
#include "hbuf.h"
#include "hmutex.h"

#include "array.h"
#include "list.h"
#include "heap.h"
#include "queue.h"

#define HLOOP_READ_BUFSIZE          8192        // 8K
#define READ_BUFSIZE_HIGH_WATER     65536       // 64K
#define WRITE_QUEUE_HIGH_WATER      (1U << 23)  // 8M

ARRAY_DECL(hio_t*, io_array);
QUEUE_DECL(hevent_t, event_queue);

struct hloop_s {
    uint32_t    flags;
    hloop_status_e status;
    uint64_t    start_ms;       // ms
    uint64_t    start_hrtime;   // us
    uint64_t    end_hrtime;
    uint64_t    cur_hrtime;
    uint64_t    loop_cnt;
    long        pid;
    long        tid;
    void*       userdata;
//private:
    // events
    uint32_t                    intern_nevents;
    uint32_t                    nactives;
    uint32_t                    npendings;
    // pendings: with priority as array.index
    hevent_t*                   pendings[HEVENT_PRIORITY_SIZE];
    // idles
    struct list_head            idles;
    uint32_t                    nidles;
    // timers
    struct heap                 timers;
    uint32_t                    ntimers;
    // ios: with fd as array.index
    struct io_array             ios;
    uint32_t                    nios;
    // one loop per thread, so one readbuf per loop is OK.
    hbuf_t                      readbuf;
    void*                       iowatcher;
    // custom_events
    int                         sockpair[2];
    event_queue                 custom_events;
    hmutex_t                    custom_events_mutex;
};

uint64_t hloop_next_event_id();

struct hidle_s {
    HEVENT_FIELDS
    uint32_t    repeat;
//private:
    struct list_node node;
};

#define HTIMER_FIELDS                   \
    HEVENT_FIELDS                       \
    uint32_t    repeat;                 \
    uint64_t    next_timeout;           \
    struct heap_node node;

struct htimer_s {
    HTIMER_FIELDS
};

struct htimeout_s {
    HTIMER_FIELDS
    uint32_t    timeout;                \
};

struct hperiod_s {
    HTIMER_FIELDS
    int8_t      minute;
    int8_t      hour;
    int8_t      day;
    int8_t      week;
    int8_t      month;
};

QUEUE_DECL(offset_buf_t, write_queue);
struct hio_s {
    HEVENT_FIELDS
    // flags
    unsigned    ready       :1;
    unsigned    closed      :1;
    unsigned    accept      :1;
    unsigned    connect     :1;
    unsigned    connectex   :1; // for ConnectEx/DisconnectEx
    unsigned    recv        :1;
    unsigned    send        :1;
    unsigned    recvfrom    :1;
    unsigned    sendto      :1;
    unsigned    close       :1;
    unsigned    read_once   :1;     // for hio_read_once
    unsigned    alloced_readbuf :1; // for hio_read_until, hio_set_unpack
// public:
    uint32_t    id; // fd cannot be used as unique identifier, so we provide an id
    int         fd;
    hio_type_e  io_type;
    int         error;
    int         events;
    int         revents;
    struct sockaddr*    localaddr;
    struct sockaddr*    peeraddr;
    offset_buf_t        readbuf;        // for read
    int                 read_until;     // for hio_read_until
    uint32_t            small_readbytes_cnt;
    struct write_queue  write_queue;    // for write
    hrecursive_mutex_t  write_mutex;    // lock write and write_queue
    uint32_t            write_queue_bytes;
    // callbacks
    hread_cb    read_cb;
    hwrite_cb   write_cb;
    hclose_cb   close_cb;
    haccept_cb  accept_cb;
    hconnect_cb connect_cb;
    // timers
    int         connect_timeout;    // ms
    htimer_t*   connect_timer;
    int         close_timeout;      // ms
    htimer_t*   close_timer;
    int         keepalive_timeout;  // ms
    htimer_t*   keepalive_timer;
    int         heartbeat_interval; // ms
    hio_send_heartbeat_fn heartbeat_fn;
    htimer_t*   heartbeat_timer;
    // upstream
    struct hio_s*   upstream_io;
    // unpack
    unpack_setting_t*   unpack_setting;
// private:
    int         event_index[2]; // for poll,kqueue
    void*       hovlp;          // for iocp/overlapio
    void*       ssl;            // for SSL
    void*       ctx;
};
/*
 * hio lifeline:
 * fd =>
 * hio_get => HV_ALLOC_SIZEOF(io) => hio_init =>
 * hio_ready => hio_add => hio_del => hio_done =>
 * hio_close => hclose_cb =>
 * hio_free => HV_FREE(io)
 */
void hio_init(hio_t* io);
void hio_ready(hio_t* io);
void hio_done(hio_t* io);
void hio_free(hio_t* io);
uint32_t hio_next_id();

void hio_accept_cb(hio_t* io);
void hio_connect_cb(hio_t* io);
void hio_read_cb(hio_t* io, void* buf, int len);
void hio_write_cb(hio_t* io, const void* buf, int len);
void hio_close_cb(hio_t* io);

void hio_del_connect_timer(hio_t* io);
void hio_del_close_timer(hio_t* io);
void hio_del_keepalive_timer(hio_t* io);
void hio_del_heartbeat_timer(hio_t* io);

static inline bool hio_is_loop_readbuf(hio_t* io) {
    return io->readbuf.base == io->loop->readbuf.base;
}
static inline bool hio_is_alloced_readbuf(hio_t* io) {
    return io->alloced_readbuf;
}
void hio_alloc_readbuf(hio_t* io, int len);
void hio_free_readbuf(hio_t* io);

#define EVENT_ENTRY(p)          container_of(p, hevent_t, pending_node)
#define IDLE_ENTRY(p)           container_of(p, hidle_t,  node)
#define TIMER_ENTRY(p)          container_of(p, htimer_t, node)

#define EVENT_ACTIVE(ev) \
    if (!ev->active) {\
        ev->active = 1;\
        ev->loop->nactives++;\
    }\

#define EVENT_INACTIVE(ev) \
    if (ev->active) {\
        ev->active = 0;\
        ev->loop->nactives--;\
    }\

#define EVENT_PENDING(ev) \
    do {\
        if (!ev->pending) {\
            ev->pending = 1;\
            ev->loop->npendings++;\
            hevent_t** phead = &ev->loop->pendings[HEVENT_PRIORITY_INDEX(ev->priority)];\
            ev->pending_next = *phead;\
            *phead = (hevent_t*)ev;\
        }\
    } while(0)

#define EVENT_ADD(loop, ev, cb) \
    do {\
        ev->loop = loop;\
        ev->event_id = hloop_next_event_id();\
        ev->cb = (hevent_cb)cb;\
        EVENT_ACTIVE(ev);\
    } while(0)

#define EVENT_DEL(ev) \
    do {\
        EVENT_INACTIVE(ev);\
        if (!ev->pending) {\
            HV_FREE(ev);\
        }\
    } while(0)

#define EVENT_RESET(ev) \
    do {\
        ev->destroy = 0;\
        EVENT_ACTIVE(ev);\
        ev->pending = 0;\
    } while(0)

#endif // HV_EVENT_H_
