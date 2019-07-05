#ifndef HW_LOOP_H_
#define HW_LOOP_H_

#include <map>

#ifndef INFINITE
#define INFINITE    (uint32_t)-1
#endif

typedef struct hloop_s  hloop_t;
typedef struct htimer_s htimer_t;
typedef struct hidle_s  hidle_t;
typedef struct hevent_s hevent_t;

typedef void (*htimer_cb)(htimer_t* timer, void* userdata);
typedef void (*hidle_cb)(hidle_t* idle, void* userdata);
typedef void (*hevent_cb)(hevent_t* event, void* userdata);

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
    // timers
    uint32_t                    timer_counter;
    // timer_id => timer
    std::map<int, htimer_t*>    timers;
    uint32_t                    min_timer_timeout;
    // idles
    uint32_t                    idle_counter;
    // hidle_id => idle
    std::map<int, hidle_t*>     idles;
    // events
    // fd => event
    std::map<int, hevent_t*>    events;
    void*                       event_ctx; // private
};

struct htimer_s {
    hloop_t*    loop;
    uint32_t    timer_id;
    uint32_t    timeout;
    uint32_t    repeat;
    htimer_cb   cb;
    void*       userdata;
//private:
    unsigned    destroy     :1;
    unsigned    disable     :1;
    uint64_t    next_timeout;
};

struct hidle_s {
    hloop_t*    loop;
    uint32_t    idle_id;
    uint32_t    repeat;
    hidle_cb    cb;
    void*       userdata;
//private:
    unsigned    destroy     :1;
    unsigned    disable     :1;
};

typedef union {
    void*       ptr;
    uint32_t    u32;
    uint64_t    u64;
} hevent_data_e;

struct hevent_s {
    hloop_t*    loop;
    int         fd;
    hevent_cb   read_cb;
    void*       read_userdata;
    hevent_cb   write_cb;
    void*       write_userdata;
//private:
    unsigned    destroy     :1;
    unsigned    disable     :1;
    unsigned    accept      :1;
    unsigned    connect     :1;
    unsigned    readable    :1;
    unsigned    writeable   :1;
    int         event_index; // for poll
    int         events;      // for epoll
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

// event
hevent_t* hevent_accept(hloop_t* loop, int listenfd, hevent_cb on_accept, void* userdata);
hevent_t* hevent_connect(hloop_t* loop, int connfd, hevent_cb on_connect, void* userdata);
hevent_t* hevent_read(hloop_t* loop, int fd, hevent_cb on_readable, void* userdata);
hevent_t* hevent_write(hloop_t* loop, int fd, hevent_cb on_writeable, void* userdata);
void      hevent_del(hloop_t* loop, int fd);
void      hevent_del(hevent_t* event);

#endif // HW_LOOP_H_
