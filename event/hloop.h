#ifndef HV_LOOP_H_
#define HV_LOOP_H_

#include "hdef.h"

BEGIN_EXTERN_C

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

typedef void (*haccept_cb)  (hio_t* io);
typedef void (*hconnect_cb) (hio_t* io);
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

#define HEVENT_LOWEST_PRIORITY    (-5)
#define HEVENT_LOW_PRIORITY       (-3)
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

#define hevent_set_priority(ev, prio)   ((hevent_t*)(ev))->priority = prio
#define hevent_set_userdata(ev, udata)  ((hevent_t*)(ev))->userdata = (void*)udata

#define hevent_loop(ev)         (((hevent_t*)(ev))->loop)
#define hevent_type(ev)         (((hevent_t*)(ev))->event_type)
#define hevent_id(ev)           (((hevent_t*)(ev))->event_id)
#define hevent_priority(ev)     (((hevent_t*)(ev))->priority)
#define hevent_userdata(ev)     (((hevent_t*)(ev))->userdata)

typedef enum {
    HIO_TYPE_UNKNOWN = 0,
    HIO_TYPE_STDIN   = 0x00000001,
    HIO_TYPE_STDOUT  = 0x00000002,
    HIO_TYPE_STDERR  = 0x00000004,
    HIO_TYPE_STDIO   = 0x0000000F,

    HIO_TYPE_FILE    = 0x00000010,

    HIO_TYPE_IP      = 0x00000100,
    HIO_TYPE_UDP     = 0x00001000,
    HIO_TYPE_TCP     = 0x00010000,
    HIO_TYPE_SSL     = 0x00020000,
    HIO_TYPE_SOCKET  = 0x00FFFF00,
} hio_type_e;

// loop
#define HLOOP_FLAG_RUN_ONCE    0x00000001
#define HLOOP_FLAG_AUTO_FREE   0x00000002
hloop_t* hloop_new(int flags DEFAULT(HLOOP_FLAG_AUTO_FREE));
// WARN: Not allow to call hloop_free when HLOOP_INIT_FLAG_AUTO_FREE set.
void hloop_free(hloop_t** pp);
// NOTE: when no active events, loop will quit.
int hloop_run(hloop_t* loop);
int hloop_stop(hloop_t* loop);
int hloop_pause(hloop_t* loop);
int hloop_resume(hloop_t* loop);

void     hloop_update_time(hloop_t* loop);
uint64_t hloop_now(hloop_t* loop);          // s
uint64_t hloop_now_ms(hloop_t* loop);       // ms
uint64_t hloop_now_hrtime(hloop_t* loop);   // us

void  hloop_set_userdata(hloop_t* loop, void* userdata);
void* hloop_userdata(hloop_t* loop);

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
void        htimer_reset(htimer_t* timer);

// io
//-----------------------low-level apis---------------------------------------
#define READ_EVENT  0x0001
#define WRITE_EVENT 0x0004
#define ALL_EVENTS  READ_EVENT|WRITE_EVENT
hio_t* hio_get(hloop_t* loop, int fd);
int    hio_add(hio_t* io, hio_cb cb, int events DEFAULT(READ_EVENT));
int    hio_del(hio_t* io, int events DEFAULT(ALL_EVENTS));

int hio_fd    (hio_t* io);
int hio_error (hio_t* io);
hio_type_e hio_type(hio_t* io);
struct sockaddr* hio_localaddr(hio_t* io);
struct sockaddr* hio_peeraddr (hio_t* io);

void hio_set_readbuf(hio_t* io, void* buf, size_t len);
// ssl
int  hio_enable_ssl(hio_t* io);

void hio_setcb_accept   (hio_t* io, haccept_cb  accept_cb);
void hio_setcb_connect  (hio_t* io, hconnect_cb connect_cb);
void hio_setcb_read     (hio_t* io, hread_cb    read_cb);
void hio_setcb_write    (hio_t* io, hwrite_cb   write_cb);
void hio_setcb_close    (hio_t* io, hclose_cb   close_cb);

// NOTE: don't forget to call hio_set_readbuf
int hio_read   (hio_t* io);
int hio_write  (hio_t* io, const void* buf, size_t len);
int hio_close  (hio_t* io);
int hio_accept (hio_t* io);
int hio_connect(hio_t* io);

//------------------high-level apis-------------------------------------------
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb);
// hio_get -> hio_setcb_write -> hio_write
hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));
// hio_get -> hio_close
void   hclose   (hloop_t* loop, int fd);

// tcp
// hio_get -> hio_setcb_accept -> hio_accept
hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb);
// hio_get -> hio_setcb_connect -> hio_connect
hio_t* hconnect (hloop_t* loop, int connfd,   hconnect_cb connect_cb);
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
hio_t* hrecv    (hloop_t* loop, int connfd, void* buf, size_t len, hread_cb read_cb);
// hio_get -> hio_setcb_write -> hio_write
hio_t* hsend    (hloop_t* loop, int connfd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));

// udp/ip
// for HIO_TYPE_IP
void hio_set_type(hio_t* io, hio_type_e type);
void hio_set_localaddr(hio_t* io, struct sockaddr* addr, int addrlen);
void hio_set_peeraddr (hio_t* io, struct sockaddr* addr, int addrlen);
// NOTE: must call hio_set_peeraddr before hrecvfrom/hsendto
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
hio_t* hrecvfrom (hloop_t* loop, int sockfd, void* buf, size_t len, hread_cb read_cb);
// hio_get -> hio_setcb_write -> hio_write
hio_t* hsendto   (hloop_t* loop, int sockfd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));

//----------------- top-level apis---------------------------------------------
// @tcp_server: socket -> bind -> listen -> haccept
hio_t* create_tcp_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);
// @tcp_client: resolver -> socket -> hio_get -> hio_set_peeraddr -> hconnect
hio_t* create_tcp_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb);

// @udp_server: socket -> bind -> hio_get
hio_t* create_udp_server (hloop_t* loop, const char* host, int port);
// @udp_client: resolver -> socket -> hio_get -> hio_set_peeraddr
hio_t* create_udp_client (hloop_t* loop, const char* host, int port);

END_EXTERN_C

#endif // HV_LOOP_H_
