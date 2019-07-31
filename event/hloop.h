#ifndef HW_LOOP_H_
#define HW_LOOP_H_

#include "hdef.h"

BEGIN_EXTERN_C

#include "htime.h"
#include "array.h"
#include "list.h"
#include "heap.h"
#include "queue.h"
#include "hbuf.h"

typedef struct hloop_s  hloop_t;
typedef struct hevent_s hevent_t;

typedef struct hidle_s      hidle_t;
typedef struct htimer_s     htimer_t;
typedef struct htimeout_s   htimeout_t;
typedef struct hperiod_s    hperiod_t;
typedef struct hio_s        hio_t;

typedef void (*hevent_cb)   (hevent_t* ev);
typedef void (*hidle_cb)    (hidle_t* idle);
typedef void (*htimer_cb)   (htimer_t* timer);
typedef void (*hio_cb)      (hio_t* io);

typedef void (*haccept_cb)  (hio_t* io, int connfd);
typedef void (*hconnect_cb) (hio_t* io, int state);
typedef void (*hread_cb)    (hio_t* io, void* buf, int readbytes);
typedef void (*hwrite_cb)   (hio_t* io, const void* buf, int writebytes);
typedef void (*hclose_cb)   (hio_t* io);

typedef enum {
    HEVENT_TYPE_NONE    = 0,
    HEVENT_TYPE_IDLE    = 0x00000010,
    HEVENT_TYPE_TIMEOUT = 0x00000100,
    HEVENT_TYPE_PERIOD  = 0x00000200,
    HEVENT_TYPE_TIMER   = HEVENT_TYPE_TIMEOUT|HEVENT_TYPE_PERIOD,
    HEVENT_TYPE_IO      = 0x00001000,
} hevent_type_e;

#define HEVENT_LOWEST_PRIORITY     -5
#define HEVENT_LOW_PRIORITY        -3
#define HEVENT_NORMAL_PRIORITY      0
#define HEVENT_HIGH_PRIORITY        3
#define HEVENT_HIGHEST_PRIORITY     5
#define HEVENT_PRIORITY_SIZE  (HEVENT_HIGHEST_PRIORITY-HEVENT_LOWEST_PRIORITY+1)
#define HEVENT_PRIORITY_INDEX(priority) (priority-HEVENT_LOWEST_PRIORITY)

#define HEVENT_FLAGS        \
    unsigned    destroy :1; \
    unsigned    active  :1; \
    unsigned    pending :1;

#define HEVENT_FIELDS                   \
    hloop_t*            loop;           \
    hevent_type_e       event_type;     \
    uint64_t            event_id;       \
    hevent_cb           cb;             \
    void*               userdata;       \
    int                 priority;       \
    struct hevent_s*    pending_next;   \
    HEVENT_FLAGS

struct hevent_s {
    HEVENT_FIELDS
};

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
    unsigned    accept      :1;
    unsigned    connect     :1;
    unsigned    closed      :1;
    unsigned    connectex   :1; // for ConnectEx/DisconnectEx
    int         fd;
    int         error;
    int         events;
    int         revents;
    struct sockaddr*    localaddr;
    struct sockaddr*    peeraddr;
    hbuf_t              readbuf;        // for hread
    struct write_queue  write_queue;    // for hwrite
    // callbacks
    hread_cb    read_cb;
    hwrite_cb   write_cb;
    hclose_cb   close_cb;
    haccept_cb  accept_cb;
    hconnect_cb connect_cb;
//private:
    int         event_index[2]; // for poll,kqueue
    void*       hovlp;          // for iocp/overlapio
};

typedef enum {
    HLOOP_STATUS_STOP,
    HLOOP_STATUS_RUNNING,
    HLOOP_STATUS_PAUSE
} hloop_status_e;

ARRAY_DECL(hio_t*, io_array);

struct hloop_s {
    hloop_status_e status;
    time_t      start_time;
    // hrtime: us
    uint64_t    start_hrtime;
    uint64_t    end_hrtime;
    uint64_t    cur_hrtime;
    uint64_t    loop_cnt;
    void*       userdata;
//private:
    // events
    uint64_t                    event_counter;
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
    void*                       iowatcher;
};

// loop
int hloop_init(hloop_t* loop);
//void hloop_cleanup(hloop_t* loop);
int hloop_run(hloop_t* loop);
int hloop_stop(hloop_t* loop);
int hloop_pause(hloop_t* loop);
int hloop_resume(hloop_t* loop);

static inline void hloop_update_time(hloop_t* loop) {
    loop->cur_hrtime = gethrtime();
}

static inline time_t hloop_now(hloop_t* loop) {
    return loop->start_time + (loop->cur_hrtime - loop->start_hrtime) / 1000000;
}

static inline uint64_t hloop_now_hrtime(hloop_t* loop) {
    return loop->start_time*1e6 + (loop->cur_hrtime - loop->start_hrtime);
}

// idle
hidle_t*    hidle_add(hloop_t* loop, hidle_cb cb, uint32_t repeat DEFAULT(INFINITE));
void        hidle_del(hidle_t* idle);

// timer
// @param timeout: unit(ms)
htimer_t*   htimer_add(hloop_t* loop, htimer_cb cb, uint64_t timeout, uint32_t repeat DEFAULT(INFINITE));
/*
 * minute   hour    day     week    month       cb
 * 0~59     0~23    1~31    0~6     1~12
 *  30      -1      -1      -1      -1          cron.hourly
 *  30      1       -1      -1      -1          cron.daily
 *  30      1       15      -1      -1          cron.monthly
 *  30      1       -1       7      -1          cron.weekly
 *  30      1        1      -1      10          cron.yearly
 */
htimer_t*   htimer_add_period(hloop_t* loop, htimer_cb cb,
                int8_t minute DEFAULT(0),  int8_t hour  DEFAULT(-1), int8_t day DEFAULT(-1),
                int8_t week   DEFAULT(-1), int8_t month DEFAULT(-1), uint32_t repeat DEFAULT(INFINITE));
void        htimer_del(htimer_t* timer);

// io
// frist level apis
#define READ_EVENT  0x0001
#define WRITE_EVENT 0x0004
#define ALL_EVENTS  READ_EVENT|WRITE_EVENT
hio_t*      hio_add(hloop_t* loop, hio_cb cb, int fd, int events DEFAULT(READ_EVENT));
void        hio_del(hio_t* io, int events DEFAULT(ALL_EVENTS));

// second level apis
hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb);
// Listen => haccept
hio_t* hlisten  (hloop_t* loop, int port,     haccept_cb accept_cb);
hio_t* hconnect (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb);
hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb);
hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));
void   hclose   (hio_t* io);

END_EXTERN_C

#endif // HW_LOOP_H_
