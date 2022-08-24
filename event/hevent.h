#ifndef HV_EVENT_H_
#define HV_EVENT_H_

#include "hloop.h"
#include "iowatcher.h"
#include "rudp.h"

#include "hbuf.h"
#include "hmutex.h"

#include "array.h"
#include "list.h"
#include "heap.h"
#include "queue.h"

#define HLOOP_READ_BUFSIZE          8192        // 8K
#define READ_BUFSIZE_HIGH_WATER     65536       // 64K
#define WRITE_BUFSIZE_HIGH_WATER    (1U << 23)  // 8M
#define MAX_READ_BUFSIZE            (1U << 24)  // 16M
#define MAX_WRITE_BUFSIZE           (1U << 24)  // 16M

// hio_read_flags
#define HIO_READ_ONCE           0x1
#define HIO_READ_UNTIL_LENGTH   0x2
#define HIO_READ_UNTIL_DELIM    0x4

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
    struct heap                 timers;     // monotonic time
    struct heap                 realtimers; // realtime
    uint32_t                    ntimers;
    // ios: with fd as array.index
    struct io_array             ios;
    uint32_t                    nios;
    // one loop per thread, so one readbuf per loop is OK.
    hbuf_t                      readbuf;
    void*                       iowatcher;
    // custom_events
    int                         eventfds[2];
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
// sizeof(struct hio_s)=416 on linux-x64
struct hio_s {
    HEVENT_FIELDS
    // flags
    unsigned    ready       :1;
    unsigned    connected   :1;
    unsigned    closed      :1;
    unsigned    accept      :1;
    unsigned    connect     :1;
    unsigned    connectex   :1; // for ConnectEx/DisconnectEx
    unsigned    recv        :1;
    unsigned    send        :1;
    unsigned    recvfrom    :1;
    unsigned    sendto      :1;
    unsigned    close       :1;
    unsigned    alloced_readbuf :1; // for hio_alloc_readbuf
    unsigned    alloced_ssl_ctx :1; // for hio_new_ssl_ctx
// public:
    hio_type_e  io_type;
    uint32_t    id; // fd cannot be used as unique identifier, so we provide an id
    int         fd;
    int         error;
    int         events;
    int         revents;
    struct sockaddr*    localaddr;
    struct sockaddr*    peeraddr;
    uint64_t            last_read_hrtime;
    uint64_t            last_write_hrtime;
    // read
    fifo_buf_t          readbuf;
    unsigned int        read_flags;
    // for hio_read_until
    union {
        unsigned int    read_until_length;
        unsigned char   read_until_delim;
    };
    uint32_t            max_read_bufsize;
    uint32_t            small_readbytes_cnt; // for readbuf autosize
    // write
    struct write_queue  write_queue;
    hrecursive_mutex_t  write_mutex; // lock write and write_queue
    uint32_t            write_bufsize;
    uint32_t            max_write_bufsize;
    // callbacks
    hread_cb    read_cb;
    hwrite_cb   write_cb;
    hclose_cb   close_cb;
    haccept_cb  accept_cb;
    hconnect_cb connect_cb;
    // timers
    int         connect_timeout;    // ms
    int         close_timeout;      // ms
    int         read_timeout;       // ms
    int         write_timeout;      // ms
    int         keepalive_timeout;  // ms
    int         heartbeat_interval; // ms
    hio_send_heartbeat_fn heartbeat_fn;
    htimer_t*   connect_timer;
    htimer_t*   close_timer;
    htimer_t*   read_timer;
    htimer_t*   write_timer;
    htimer_t*   keepalive_timer;
    htimer_t*   heartbeat_timer;
    // upstream
    struct hio_s*       upstream_io;    // for hio_setup_upstream
    // unpack
    unpack_setting_t*   unpack_setting; // for hio_set_unpack
    // ssl
    void*       ssl;        // for hio_set_ssl
    void*       ssl_ctx;    // for hio_set_ssl_ctx
    char*       hostname;   // for hssl_set_sni_hostname
    // context
    void*       ctx; // for hio_context / hio_set_context
// private:
#if defined(EVENT_POLL) || defined(EVENT_KQUEUE)
    int         event_index[2]; // for poll,kqueue
#endif

#ifdef EVENT_IOCP
    void*       hovlp;          // for iocp/overlapio
#endif

#if WITH_RUDP
    rudp_t          rudp;
#if WITH_KCP
    kcp_setting_t*  kcp_setting;
#endif
#endif
};
/*
 * hio lifeline:
 *
 * fd =>
 * hio_get => HV_ALLOC_SIZEOF(io) => hio_init => hio_ready
 *
 * hio_read  => hio_add(HV_READ) => hio_read_cb
 * hio_write => hio_add(HV_WRITE) => hio_write_cb
 * hio_close => hio_done => hio_del(HV_RDWR) => hio_close_cb
 *
 * hloop_stop => hloop_free => hio_free => HV_FREE(io)
 */
void hio_init(hio_t* io);
void hio_ready(hio_t* io);
void hio_done(hio_t* io);
void hio_free(hio_t* io);
uint32_t hio_next_id();

void hio_accept_cb(hio_t* io);
void hio_connect_cb(hio_t* io);
void hio_handle_read(hio_t* io, void* buf, int readbytes);
void hio_read_cb(hio_t* io, void* buf, int len);
void hio_write_cb(hio_t* io, const void* buf, int len);
void hio_close_cb(hio_t* io);

void hio_del_connect_timer(hio_t* io);
void hio_del_close_timer(hio_t* io);
void hio_del_read_timer(hio_t* io);
void hio_del_write_timer(hio_t* io);
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
void hio_memmove_readbuf(hio_t* io);

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
