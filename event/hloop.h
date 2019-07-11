#ifndef HW_LOOP_H_
#define HW_LOOP_H_

#include "hdef.h"

#include <map>
#include <list>

typedef struct hloop_s  hloop_t;
typedef struct hevent_s hevent_t;
typedef struct htimer_s htimer_t;
typedef struct hidle_s  hidle_t;
typedef struct hio_s    hio_t;

typedef void (*hevent_cb)   (hevent_t* ev,      void* userdata);
typedef void (*htimer_cb)   (htimer_t* timer,   void* userdata);
typedef void (*hidle_cb)    (hidle_t* idle,     void* userdata);
typedef void (*hio_cb)      (hio_t* io,         void* userdata);

typedef void (*hread_cb)    (hio_t* io, void* buf, int readbytes, void* userdata);
typedef void (*hwrite_cb)   (hio_t* io, const void* buf, int writebytes, void* userdata);
typedef void (*hclose_cb)   (hio_t* io, void* userdata);
typedef void (*haccept_cb)  (hio_t* io, int connfd, void* userdata);
typedef void (*hconnect_cb) (hio_t* io, int state,  void* userdata);

typedef enum {
    HLOOP_STATUS_STOP,
    HLOOP_STATUS_RUNNING,
    HLOOP_STATUS_PAUSE
} hloop_status_e;

struct hloop_s {
    hloop_status_e status;
    uint64_t    start_time;
    uint64_t    end_time;
    uint64_t    cur_time;
    uint64_t    loop_cnt;
    var         custom_data;
//private:
    uint64_t                    event_counter;
    // timers
    // timer_id => timer
    uint32_t                    timer_counter;
    std::map<int, htimer_t*>    timers;
    uint32_t                    min_timer_timeout;
    // idles
    // hidle_id => idle
    uint32_t                    idle_counter;
    std::map<int, hidle_t*>     idles;
    // ios
    // fd => io
    std::map<int, hio_t*>       ios;
    void*                       iowatcher;
};

typedef enum {
    HEVENT_TYPE_NONE    = 0,
    HEVENT_TYPE_TIMER   = 0x0001,
    HEVENT_TYPE_IDLE    = 0x0002,
    HEVENT_TYPE_IO      = 0x0004,
} hevent_type_e;

#define HEVENT_FIELDS               \
    hloop_t*        loop;           \
    hevent_type_e   event_type;     \
    uint64_t        event_id;       \
    int             priority;       \
    var             custom_data;

#define HEVENT_FLAGS        \
    unsigned    destroy :1; \
    unsigned    active  :1; \
    unsigned    pending :1;

struct hevent_s {
    HEVENT_FIELDS
//private:
    HEVENT_FLAGS
};

struct htimer_s {
    HEVENT_FIELDS
    uint32_t    timer_id;
    uint32_t    timeout;
    uint32_t    repeat;
    htimer_cb   cb;
    void*       userdata;
//private:
    uint64_t    next_timeout;
    HEVENT_FLAGS
};

struct hidle_s {
    HEVENT_FIELDS
    uint32_t    idle_id;
    uint32_t    repeat;
    hidle_cb    cb;
    void*       userdata;
//private:
    HEVENT_FLAGS
};

struct hio_s {
    HEVENT_FIELDS
    int         fd;
    int         error;
    char*       readbuf;
    int         readbuflen;
    // callbacks
    hread_cb    read_cb;
    void*       read_userdata;
    hwrite_cb   write_cb;
    void*       write_userdata;
    hclose_cb   close_cb;
    void*       close_userdata;
    haccept_cb  accept_cb;
    void*       accept_userdata;
    hconnect_cb connect_cb;
    void*       connect_userdata;
//private:
    hio_cb      revent_cb;
    void*       revent_userdata;
    hio_cb      wevent_cb;
    void*       wevent_userdata;
    int         event_index[2];
    int         events;
    int         revents;
    HEVENT_FLAGS
    unsigned    accept      :1;
    unsigned    connect     :1;
};

// loop
int hloop_init(hloop_t* loop);
//void hloop_cleanup(hloop_t* loop);
int hloop_run(hloop_t* loop);
int hloop_stop(hloop_t* loop);
int hloop_pause(hloop_t* loop);
int hloop_resume(hloop_t* loop);

// timer
// @param timeout: unit(ms)
htimer_t*   htimer_add(hloop_t* loop, htimer_cb cb, void* userdata, uint64_t timeout, uint32_t repeat = INFINITE);
void        htimer_del(hloop_t* loop, uint32_t timer_id);
void        htimer_del(htimer_t* timer);

// idle
hidle_t*    hidle_add(hloop_t* loop, hidle_cb cb, void* userdata, uint32_t repeat = INFINITE);
void        hidle_del(hloop_t* loop, uint32_t idle_id);
void        hidle_del(hidle_t* idle);

// io
hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb, void* accept_userdata,
                    hclose_cb close_cb = NULL, void* close_userdata = NULL);
hio_t* hconnect (hloop_t* loop, int connfd, hconnect_cb connect_cb, void* connect_userdata,
                    hclose_cb close_cb = NULL, void* close_userdata = NULL);
hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb, void* read_userdata,
                    hclose_cb close_cb = NULL, void* close_userdata = NULL);
hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len,
                    hwrite_cb write_cb = NULL, void* write_userdata = NULL,
                    hclose_cb close_cb = NULL, void* close_userdata = NULL);
void   hclose   (hio_t* io);

#endif // HW_LOOP_H_
