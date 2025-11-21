#ifndef HV_LOOP_H_
#define HV_LOOP_H_

#include "hexport.h"
#include "hplatform.h"
#include "hdef.h"
#include "hssl.h"

typedef struct hloop_s      hloop_t;
typedef struct hevent_s     hevent_t;

// NOTE: The following structures are subclasses of hevent_t,
// inheriting hevent_t data members and function members.
typedef struct hio_s        hio_t;
typedef struct hidle_s      hidle_t;
typedef struct htimer_s     htimer_t;
typedef struct htimeout_s   htimeout_t;
typedef struct hperiod_s    hperiod_t;
typedef struct hevent_s     hsignal_t;

typedef void (*hevent_cb)   (hevent_t* ev);
typedef void (*hio_cb)      (hio_t* io);
typedef void (*hidle_cb)    (hidle_t* idle);
typedef void (*htimer_cb)   (htimer_t* timer);
typedef void (*hsignal_cb)  (hsignal_t* sig);

typedef void (*haccept_cb)  (hio_t* io);
typedef void (*hconnect_cb) (hio_t* io);
typedef void (*hread_cb)    (hio_t* io, void* buf, int readbytes);
typedef void (*hwrite_cb)   (hio_t* io, const void* buf, int writebytes);
typedef void (*hclose_cb)   (hio_t* io);

typedef enum {
    HLOOP_STATUS_STOP,
    HLOOP_STATUS_RUNNING,
    HLOOP_STATUS_PAUSE,
    HLOOP_STATUS_DESTROY
} hloop_status_e;

typedef enum {
    HEVENT_TYPE_NONE    = 0,
    HEVENT_TYPE_IO      = 0x00000001,
    HEVENT_TYPE_TIMEOUT = 0x00000010,
    HEVENT_TYPE_PERIOD  = 0x00000020,
    HEVENT_TYPE_TIMER   = HEVENT_TYPE_TIMEOUT|HEVENT_TYPE_PERIOD,
    HEVENT_TYPE_IDLE    = 0x00000100,
    HEVENT_TYPE_SIGNAL  = 0x00000200,
    HEVENT_TYPE_CUSTOM  = 0x00000400, // 1024
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
    void*               privdata;       \
    struct hevent_s*    pending_next;   \
    int                 priority;       \
    HEVENT_FLAGS

// sizeof(struct hevent_s)=64 on x64
struct hevent_s {
    HEVENT_FIELDS
};

#define hevent_set_id(ev, id)           ((hevent_t*)(ev))->event_id = id
#define hevent_set_cb(ev, cb)           ((hevent_t*)(ev))->cb = cb
#define hevent_set_priority(ev, prio)   ((hevent_t*)(ev))->priority = prio
#define hevent_set_userdata(ev, udata)  ((hevent_t*)(ev))->userdata = (void*)udata

#define hevent_loop(ev)         (((hevent_t*)(ev))->loop)
#define hevent_type(ev)         (((hevent_t*)(ev))->event_type)
#define hevent_id(ev)           (((hevent_t*)(ev))->event_id)
#define hevent_cb(ev)           (((hevent_t*)(ev))->cb)
#define hevent_priority(ev)     (((hevent_t*)(ev))->priority)
#define hevent_userdata(ev)     (((hevent_t*)(ev))->userdata)

typedef enum {
    HIO_TYPE_UNKNOWN    = 0,
    HIO_TYPE_STDIN      = 0x00000001,
    HIO_TYPE_STDOUT     = 0x00000002,
    HIO_TYPE_STDERR     = 0x00000004,
    HIO_TYPE_STDIO      = 0x0000000F,

    HIO_TYPE_FILE       = 0x00000010,
    HIO_TYPE_PIPE       = 0x00000020,

    HIO_TYPE_IP         = 0x00000100,
    HIO_TYPE_SOCK_RAW   = 0x00000F00,

    HIO_TYPE_UDP        = 0x00001000,
    HIO_TYPE_KCP        = 0x00002000,
    HIO_TYPE_DTLS       = 0x00010000,
    HIO_TYPE_SOCK_DGRAM = 0x000FF000,

    HIO_TYPE_TCP        = 0x00100000,
    HIO_TYPE_SSL        = 0x01000000,
    HIO_TYPE_TLS        = HIO_TYPE_SSL,
    HIO_TYPE_SOCK_STREAM= 0x0FF00000,

    HIO_TYPE_SOCKET     = 0x0FFFFF00,
} hio_type_e;

typedef enum {
    HIO_SERVER_SIDE  = 0,
    HIO_CLIENT_SIDE  = 1,
} hio_side_e;

#define HIO_DEFAULT_CONNECT_TIMEOUT     10000   // ms
#define HIO_DEFAULT_CLOSE_TIMEOUT       60000   // ms
#define HIO_DEFAULT_KEEPALIVE_TIMEOUT   75000   // ms
#define HIO_DEFAULT_HEARTBEAT_INTERVAL  10000   // ms

BEGIN_EXTERN_C

// loop
#define HLOOP_FLAG_RUN_ONCE                     0x00000001
#define HLOOP_FLAG_AUTO_FREE                    0x00000002
#define HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS   0x00000004
HV_EXPORT hloop_t* hloop_new(int flags DEFAULT(HLOOP_FLAG_AUTO_FREE));

// WARN: Forbid to call hloop_free if HLOOP_FLAG_AUTO_FREE set.
HV_EXPORT void hloop_free(hloop_t** pp);

HV_EXPORT int hloop_process_events(hloop_t* loop, int timeout_ms DEFAULT(0));

// NOTE: when no active events, loop will quit if HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS set.
HV_EXPORT int hloop_run(hloop_t* loop);
// NOTE: hloop_stop called in loop-thread just set flag to quit in next loop,
// if called in other thread, it will wakeup loop-thread from blocking poll system call,
// then you should join loop thread to safely exit loop thread.
HV_EXPORT int hloop_stop(hloop_t* loop);
HV_EXPORT int hloop_pause(hloop_t* loop);
HV_EXPORT int hloop_resume(hloop_t* loop);
HV_EXPORT int hloop_wakeup(hloop_t* loop);
HV_EXPORT hloop_status_e hloop_status(hloop_t* loop);

HV_EXPORT void     hloop_update_time(hloop_t* loop);
HV_EXPORT uint64_t hloop_now(hloop_t* loop);        // s
HV_EXPORT uint64_t hloop_now_ms(hloop_t* loop);     // ms
HV_EXPORT uint64_t hloop_now_us(hloop_t* loop);     // us
HV_EXPORT uint64_t hloop_now_hrtime(hloop_t* loop); // us

// export some hloop's members
// @return pid of hloop_run
HV_EXPORT long hloop_pid(hloop_t* loop);
// @return tid of hloop_run
HV_EXPORT long hloop_tid(hloop_t* loop);
// @return count of loop
HV_EXPORT uint64_t hloop_count(hloop_t* loop);
// @return number of ios
HV_EXPORT uint32_t hloop_nios(hloop_t* loop);
// @return number of timers
HV_EXPORT uint32_t hloop_ntimers(hloop_t* loop);
// @return number of idles
HV_EXPORT uint32_t hloop_nidles(hloop_t* loop);
// @return number of active events
HV_EXPORT uint32_t hloop_nactives(hloop_t* loop);

// userdata
HV_EXPORT void  hloop_set_userdata(hloop_t* loop, void* userdata);
HV_EXPORT void* hloop_userdata(hloop_t* loop);

// custom_event
/*
 * hevent_t ev;
 * memset(&ev, 0, sizeof(hevent_t));
 * ev.event_type = (hevent_type_e)(HEVENT_TYPE_CUSTOM + 1);
 * ev.cb = custom_event_cb;
 * ev.userdata = userdata;
 * hloop_post_event(loop, &ev);
 */
// NOTE: hloop_post_event is thread-safe, used to post event from other thread to loop thread.
HV_EXPORT void hloop_post_event(hloop_t* loop, hevent_t* ev);

// signal
HV_EXPORT hsignal_t* hsignal_add(hloop_t* loop, hsignal_cb cb, int signo);
HV_EXPORT void       hsignal_del(hsignal_t* sig);

// idle
HV_EXPORT hidle_t* hidle_add(hloop_t* loop, hidle_cb cb, uint32_t repeat DEFAULT(INFINITE));
HV_EXPORT void     hidle_del(hidle_t* idle);

// timer
HV_EXPORT htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint32_t timeout_ms, uint32_t repeat DEFAULT(INFINITE));
/*
 * minute   hour    day     week    month       cb
 * 0~59     0~23    1~31    0~6     1~12
 *  -1      -1      -1      -1      -1          cron.minutely
 *  30      -1      -1      -1      -1          cron.hourly
 *  30      1       -1      -1      -1          cron.daily
 *  30      1       15      -1      -1          cron.monthly
 *  30      1       -1       5      -1          cron.weekly
 *  30      1        1      -1      10          cron.yearly
 */
HV_EXPORT htimer_t* htimer_add_period(hloop_t* loop, htimer_cb cb,
                        int8_t minute DEFAULT(0),  int8_t hour  DEFAULT(-1), int8_t day DEFAULT(-1),
                        int8_t week   DEFAULT(-1), int8_t month DEFAULT(-1), uint32_t repeat DEFAULT(INFINITE));

HV_EXPORT void htimer_del(htimer_t* timer);
HV_EXPORT void htimer_reset(htimer_t* timer, uint32_t timeout_ms DEFAULT(0));

// io
//-----------------------low-level apis---------------------------------------
#define HV_READ  0x0001
#define HV_WRITE 0x0004
#define HV_RDWR  (HV_READ|HV_WRITE)
/*
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
*/
HV_EXPORT const char* hio_engine();

HV_EXPORT hio_t* hio_get(hloop_t* loop, int fd);
HV_EXPORT int    hio_add(hio_t* io, hio_cb cb, int events DEFAULT(HV_READ));
HV_EXPORT int    hio_del(hio_t* io, int events DEFAULT(HV_RDWR));

// NOTE: io detach from old loop and attach to new loop
/* @see examples/multi-thread/one-acceptor-multi-workers.c
void new_conn_event(hevent_t* ev) {
    hloop_t* loop = ev->loop;
    hio_t* io = (hio_t*)hevent_userdata(ev);
    hio_attach(loop, io);
}

void on_accpet(hio_t* io) {
    hio_detach(io);

    hloop_t* worker_loop = get_one_loop();
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.loop = worker_loop;
    ev.cb = new_conn_event;
    ev.userdata = io;
    hloop_post_event(worker_loop, &ev);
}
 */
HV_EXPORT void hio_detach(/*hloop_t* loop,*/ hio_t* io);
HV_EXPORT void hio_attach(hloop_t* loop, hio_t* io);
HV_EXPORT bool hio_exists(hloop_t* loop, int fd);

// hio_t fields
// NOTE: fd cannot be used as unique identifier, so we provide an id.
HV_EXPORT uint32_t hio_id (hio_t* io);
HV_EXPORT int hio_fd      (hio_t* io);
HV_EXPORT int hio_error   (hio_t* io);
HV_EXPORT int hio_events  (hio_t* io);
HV_EXPORT int hio_revents (hio_t* io);
HV_EXPORT hio_type_e       hio_type     (hio_t* io);
HV_EXPORT struct sockaddr* hio_localaddr(hio_t* io);
HV_EXPORT struct sockaddr* hio_peeraddr (hio_t* io);
HV_EXPORT void hio_set_context(hio_t* io, void* ctx);
HV_EXPORT void* hio_context(hio_t* io);
HV_EXPORT bool hio_is_opened(hio_t* io);
HV_EXPORT bool hio_is_connected(hio_t* io);
HV_EXPORT bool hio_is_closed(hio_t* io);

// iobuf
// #include "hbuf.h"
typedef struct fifo_buf_s hio_readbuf_t;
// NOTE: One loop per thread, one readbuf per loop.
// But you can pass in your own readbuf instead of the default readbuf to avoid memcopy.
HV_EXPORT void hio_set_readbuf(hio_t* io, void* buf, size_t len);
HV_EXPORT hio_readbuf_t* hio_get_readbuf(hio_t* io);
HV_EXPORT void hio_set_max_read_bufsize (hio_t* io, uint32_t size);
HV_EXPORT void hio_set_max_write_bufsize(hio_t* io, uint32_t size);
// NOTE: hio_write is non-blocking, so there is a write queue inside hio_t to cache unwritten data and wait for writable.
// @return current buffer size of write queue.
HV_EXPORT size_t   hio_write_bufsize(hio_t* io);
#define hio_write_is_complete(io) (hio_write_bufsize(io) == 0)

HV_EXPORT uint64_t hio_last_read_time(hio_t* io);   // ms
HV_EXPORT uint64_t hio_last_write_time(hio_t* io);  // ms

// set callbacks
HV_EXPORT void hio_setcb_accept   (hio_t* io, haccept_cb  accept_cb);
HV_EXPORT void hio_setcb_connect  (hio_t* io, hconnect_cb connect_cb);
HV_EXPORT void hio_setcb_read     (hio_t* io, hread_cb    read_cb);
HV_EXPORT void hio_setcb_write    (hio_t* io, hwrite_cb   write_cb);
HV_EXPORT void hio_setcb_close    (hio_t* io, hclose_cb   close_cb);
// get callbacks
HV_EXPORT haccept_cb  hio_getcb_accept(hio_t* io);
HV_EXPORT hconnect_cb hio_getcb_connect(hio_t* io);
HV_EXPORT hread_cb    hio_getcb_read(hio_t* io);
HV_EXPORT hwrite_cb   hio_getcb_write(hio_t* io);
HV_EXPORT hclose_cb   hio_getcb_close(hio_t* io);

// Enable SSL/TLS is so easy :)
HV_EXPORT int  hio_enable_ssl(hio_t* io);
HV_EXPORT bool hio_is_ssl(hio_t* io);
HV_EXPORT int  hio_set_ssl    (hio_t* io, hssl_t ssl);
HV_EXPORT int  hio_set_ssl_ctx(hio_t* io, hssl_ctx_t ssl_ctx);
// hssl_ctx_new(opt) -> hio_set_ssl_ctx
HV_EXPORT int  hio_new_ssl_ctx(hio_t* io, hssl_ctx_opt_t* opt);
HV_EXPORT hssl_t     hio_get_ssl(hio_t* io);
HV_EXPORT hssl_ctx_t hio_get_ssl_ctx(hio_t* io);
// for hssl_set_sni_hostname
HV_EXPORT int         hio_set_hostname(hio_t* io, const char* hostname);
HV_EXPORT const char* hio_get_hostname(hio_t* io);

// connect timeout => hclose_cb
HV_EXPORT void hio_set_connect_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_CONNECT_TIMEOUT));
// close timeout => hclose_cb
HV_EXPORT void hio_set_close_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_CLOSE_TIMEOUT));
// read timeout => hclose_cb
HV_EXPORT void hio_set_read_timeout(hio_t* io, int timeout_ms);
// write timeout => hclose_cb
HV_EXPORT void hio_set_write_timeout(hio_t* io, int timeout_ms);
// keepalive timeout => hclose_cb
HV_EXPORT void hio_set_keepalive_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_KEEPALIVE_TIMEOUT));
/*
void send_heartbeat(hio_t* io) {
    static char buf[] = "PING\r\n";
    hio_write(io, buf, 6);
}
hio_set_heartbeat(io, 3000, send_heartbeat);
*/
typedef void (*hio_send_heartbeat_fn)(hio_t* io);
// heartbeat interval => hio_send_heartbeat_fn
HV_EXPORT void hio_set_heartbeat(hio_t* io, int interval_ms, hio_send_heartbeat_fn fn);

// Nonblocking, poll IO events in the loop to call corresponding callback.
// hio_add(io, HV_READ) => accept => haccept_cb
HV_EXPORT int hio_accept (hio_t* io);

// connect => hio_add(io, HV_WRITE) => hconnect_cb
HV_EXPORT int hio_connect(hio_t* io);

// hio_add(io, HV_READ) => read => hread_cb
HV_EXPORT int hio_read   (hio_t* io);
#define hio_read_start(io) hio_read(io)
#define hio_read_stop(io)  hio_del(io, HV_READ)

// hio_read_start => hread_cb => hio_read_stop
HV_EXPORT int hio_read_once (hio_t* io);
// hio_read_once => hread_cb(len)
HV_EXPORT int hio_read_until_length(hio_t* io, unsigned int len);
// hio_read_once => hread_cb(...delim)
HV_EXPORT int hio_read_until_delim (hio_t* io, unsigned char delim);
HV_EXPORT int hio_read_remain(hio_t* io);
// @see examples/tinyhttpd.c examples/tinyproxyd.c
#define hio_readline(io)        hio_read_until_delim(io, '\n')
#define hio_readstring(io)      hio_read_until_delim(io, '\0')
#define hio_readbytes(io, len)  hio_read_until_length(io, len)
#define hio_read_until(io, len) hio_read_until_length(io, len)

// NOTE: hio_write is thread-safe, locked by recursive_mutex, allow to be called by other threads.
// hio_try_write => hio_add(io, HV_WRITE) => write => hwrite_cb
HV_EXPORT int hio_write  (hio_t* io, const void* buf, size_t len);
HV_EXPORT int hio_sendto (hio_t* io, const void* buf, size_t len, struct sockaddr* addr);

// NOTE: hio_close is thread-safe, hio_close_async will be called actually in other thread.
// hio_del(io, HV_RDWR) => close => hclose_cb
HV_EXPORT int hio_close  (hio_t* io);
// NOTE: hloop_post_event(hio_close_event)
HV_EXPORT int hio_close_async(hio_t* io);

//------------------high-level apis-------------------------------------------
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
HV_EXPORT hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb);
// hio_get -> hio_setcb_write -> hio_write
HV_EXPORT hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));
// hio_get -> hio_close
HV_EXPORT void   hclose   (hloop_t* loop, int fd);

// tcp
// hio_get -> hio_setcb_accept -> hio_accept
HV_EXPORT hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb);
// hio_get -> hio_setcb_connect -> hio_connect
HV_EXPORT hio_t* hconnect (hloop_t* loop, int connfd,   hconnect_cb connect_cb);
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
HV_EXPORT hio_t* hrecv    (hloop_t* loop, int connfd, void* buf, size_t len, hread_cb read_cb);
// hio_get -> hio_setcb_write -> hio_write
HV_EXPORT hio_t* hsend    (hloop_t* loop, int connfd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));

// udp
HV_EXPORT void hio_set_type(hio_t* io, hio_type_e type);
HV_EXPORT void hio_set_localaddr(hio_t* io, struct sockaddr* addr, int addrlen);
HV_EXPORT void hio_set_peeraddr (hio_t* io, struct sockaddr* addr, int addrlen);
// NOTE: must call hio_set_peeraddr before hrecvfrom/hsendto
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
HV_EXPORT hio_t* hrecvfrom (hloop_t* loop, int sockfd, void* buf, size_t len, hread_cb read_cb);
// hio_get -> hio_setcb_write -> hio_write
HV_EXPORT hio_t* hsendto   (hloop_t* loop, int sockfd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));

//-----------------top-level apis---------------------------------------------
// @hio_create_socket: socket -> bind -> listen
// sockaddr_set_ipport -> socket -> hio_get(loop, sockfd) ->
// side == HIO_SERVER_SIDE ? bind ->
// type & HIO_TYPE_SOCK_STREAM ? listen ->
HV_EXPORT hio_t* hio_create_socket(hloop_t* loop, const char* host, int port,
                            hio_type_e type DEFAULT(HIO_TYPE_TCP),
                            hio_side_e side DEFAULT(HIO_SERVER_SIDE));

// @tcp_server: hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_SERVER_SIDE) -> hio_setcb_accept -> hio_accept
// @see examples/tcp_echo_server.c
HV_EXPORT hio_t* hloop_create_tcp_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);

// @tcp_client: hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE) -> hio_setcb_connect -> hio_setcb_close -> hio_connect
// @see examples/nc.c
HV_EXPORT hio_t* hloop_create_tcp_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb, hclose_cb close_cb);

// @ssl_server: hio_create_socket(loop, host, port, HIO_TYPE_SSL, HIO_SERVER_SIDE) -> hio_setcb_accept -> hio_accept
// @see examples/tcp_echo_server.c => #define TEST_SSL 1
HV_EXPORT hio_t* hloop_create_ssl_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);

// @ssl_client: hio_create_socket(loop, host, port, HIO_TYPE_SSL, HIO_CLIENT_SIDE) -> hio_setcb_connect -> hio_setcb_close -> hio_connect
// @see examples/nc.c => #define TEST_SSL 1
HV_EXPORT hio_t* hloop_create_ssl_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb, hclose_cb close_cb);

// @udp_server: hio_create_socket(loop, host, port, HIO_TYPE_UDP, HIO_SERVER_SIDE)
// @see examples/udp_echo_server.c
HV_EXPORT hio_t* hloop_create_udp_server (hloop_t* loop, const char* host, int port);

// @udp_server: hio_create_socket(loop, host, port, HIO_TYPE_UDP, HIO_CLIENT_SIDE)
// @see examples/nc.c
HV_EXPORT hio_t* hloop_create_udp_client (hloop_t* loop, const char* host, int port);

//-----------------pipe---------------------------------------------
// @see examples/pipe_test.c
HV_EXPORT int hio_create_pipe(hloop_t* loop, hio_t* pipeio[2]);

//-----------------upstream---------------------------------------------
// hio_read(io)
// hio_read(io->upstream_io)
HV_EXPORT void   hio_read_upstream(hio_t* io);
// on_write(io) -> hio_write_is_complete(io) -> hio_read(io->upstream_io)
HV_EXPORT void   hio_read_upstream_on_write_complete(hio_t* io, const void* buf, int writebytes);
// hio_write(io->upstream_io, buf, bytes)
HV_EXPORT void   hio_write_upstream(hio_t* io, void* buf, int bytes);
// hio_close(io->upstream_io)
HV_EXPORT void   hio_close_upstream(hio_t* io);

// io1->upstream_io = io2;
// io2->upstream_io = io1;
// @see examples/socks5_proxy_server.c
HV_EXPORT void   hio_setup_upstream(hio_t* io1, hio_t* io2);

// @return io->upstream_io
HV_EXPORT hio_t* hio_get_upstream(hio_t* io);

// @tcp_upstream: hio_create_socket -> hio_setup_upstream -> hio_connect -> on_connect -> hio_read_upstream
// @return upstream_io
// @see examples/tcp_proxy_server.c
HV_EXPORT hio_t* hio_setup_tcp_upstream(hio_t* io, const char* host, int port, int ssl DEFAULT(0));
#define hio_setup_ssl_upstream(io, host, port) hio_setup_tcp_upstream(io, host, port, 1)

// @udp_upstream: hio_create_socket -> hio_setup_upstream -> hio_read_upstream
// @return upstream_io
// @see examples/udp_proxy_server.c
HV_EXPORT hio_t* hio_setup_udp_upstream(hio_t* io, const char* host, int port);

//-----------------unpack---------------------------------------------
typedef enum {
    UNPACK_MODE_NONE        = 0,
    UNPACK_BY_FIXED_LENGTH  = 1,    // Not recommended
    UNPACK_BY_DELIMITER     = 2,    // Suitable for text protocol
    UNPACK_BY_LENGTH_FIELD  = 3,    // Suitable for binary protocol
} unpack_mode_e;

#define DEFAULT_PACKAGE_MAX_LENGTH  (1 << 21)   // 2M

// UNPACK_BY_DELIMITER
#define PACKAGE_MAX_DELIMITER_BYTES 8

// UNPACK_BY_LENGTH_FIELD
typedef enum {
    ENCODE_BY_VARINT        = 17,               // 1 MSB + 7 bits
    ENCODE_BY_LITTEL_ENDIAN = LITTLE_ENDIAN,    // 1234
    ENCODE_BY_BIG_ENDIAN    = BIG_ENDIAN,       // 4321
    ENCODE_BY_ASN1          = 80,               // asn1 decode int
} unpack_coding_e;

typedef struct unpack_setting_s {
    unpack_mode_e   mode;
    unsigned int    package_max_length;
    union {
        // UNPACK_BY_FIXED_LENGTH
        struct {
            unsigned int    fixed_length;
        };
        // UNPACK_BY_DELIMITER
        struct {
            unsigned char   delimiter[PACKAGE_MAX_DELIMITER_BYTES];
            unsigned short  delimiter_bytes;
        };
        /*
         * UNPACK_BY_LENGTH_FIELD
         *
         * package_len = head_len + body_len + length_adjustment
         *
         * if (length_field_coding == ENCODE_BY_VARINT || length_field_coding == ENCODE_BY_ASN1) head_len = body_offset + varint_bytes - length_field_bytes;
         * else head_len = body_offset;
         *
         * length_field stores body length, exclude head length,
         * if length_field = head_len + body_len, then length_adjustment should be set to -head_len.
         *
         */
        struct {
            unsigned short  body_offset; // Equal to head length usually
            unsigned short  length_field_offset;
            unsigned short  length_field_bytes;
                     short  length_adjustment;
            unpack_coding_e length_field_coding;
        };
    };
#ifdef __cplusplus
    unpack_setting_s() {
        // Recommended setting:
        // head = flags:1byte + length:4bytes = 5bytes
        mode = UNPACK_BY_LENGTH_FIELD;
        package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
        fixed_length = 0;
        delimiter_bytes = 0;
        body_offset = 5;
        length_field_offset = 1;
        length_field_bytes = 4;
        length_field_coding = ENCODE_BY_BIG_ENDIAN;
        length_adjustment = 0;
    }
#endif
} unpack_setting_t;

/*
 * @see examples/jsonrpc examples/protorpc
 *
 * NOTE: unpack_setting_t of multiple IOs of the same function also are same,
 *       so only the pointer of unpack_setting_t is stored in hio_t,
 *       the life time of unpack_setting_t shoud be guaranteed by caller.
 */
HV_EXPORT void hio_set_unpack(hio_t* io, unpack_setting_t* setting);
HV_EXPORT void hio_unset_unpack(hio_t* io);

// unpack examples
/*
unpack_setting_t ftp_unpack_setting;
memset(&ftp_unpack_setting, 0, sizeof(unpack_setting_t));
ftp_unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
ftp_unpack_setting.mode = UNPACK_BY_DELIMITER;
ftp_unpack_setting.delimiter[0] = '\r';
ftp_unpack_setting.delimiter[1] = '\n';
ftp_unpack_setting.delimiter_bytes = 2;

unpack_setting_t mqtt_unpack_setting = {
    .mode = UNPACK_BY_LENGTH_FIELD,
    .package_max_length = DEFAULT_PACKAGE_MAX_LENGTH,
    .body_offset = 2,
    .length_field_offset = 1,
    .length_field_bytes = 1,
    .length_field_coding = ENCODE_BY_VARINT,
};

unpack_setting_t grpc_unpack_setting = {
    .mode = UNPACK_BY_LENGTH_FIELD,
    .package_max_length = DEFAULT_PACKAGE_MAX_LENGTH,
    .body_offset = 5,
    .length_field_offset = 1,
    .length_field_bytes = 4,
    .length_field_coding = ENCODE_BY_BIG_ENDIAN,
};
*/

//-----------------reconnect----------------------------------------
#define DEFAULT_RECONNECT_MIN_DELAY     1000    // ms
#define DEFAULT_RECONNECT_MAX_DELAY     60000   // ms
#define DEFAULT_RECONNECT_DELAY_POLICY  2       // exponential
#define DEFAULT_RECONNECT_MAX_RETRY_CNT INFINITE
typedef struct reconn_setting_s {
    uint32_t min_delay;  // ms
    uint32_t max_delay;  // ms
    uint32_t cur_delay;  // ms
    /*
     * @delay_policy
     * 0: fixed
     * min_delay=3s => 3,3,3...
     * 1: linear
     * min_delay=3s max_delay=10s => 3,6,9,10,10...
     * other: exponential
     * min_delay=3s max_delay=60s delay_policy=2 => 3,6,12,24,48,60,60...
     */
    uint32_t delay_policy;
    uint32_t max_retry_cnt;
    uint32_t cur_retry_cnt;

#ifdef __cplusplus
    reconn_setting_s() {
        min_delay = DEFAULT_RECONNECT_MIN_DELAY;
        max_delay = DEFAULT_RECONNECT_MAX_DELAY;
        cur_delay = 0;
        // 1,2,4,8,16,32,60,60...
        delay_policy = DEFAULT_RECONNECT_DELAY_POLICY;
        max_retry_cnt = DEFAULT_RECONNECT_MAX_RETRY_CNT;
        cur_retry_cnt = 0;
    }
#endif
} reconn_setting_t;

HV_INLINE void reconn_setting_init(reconn_setting_t* reconn) {
    reconn->min_delay = DEFAULT_RECONNECT_MIN_DELAY;
    reconn->max_delay = DEFAULT_RECONNECT_MAX_DELAY;
    reconn->cur_delay = 0;
    // 1,2,4,8,16,32,60,60...
    reconn->delay_policy = DEFAULT_RECONNECT_DELAY_POLICY;
    reconn->max_retry_cnt = DEFAULT_RECONNECT_MAX_RETRY_CNT;
    reconn->cur_retry_cnt = 0;
}

HV_INLINE void reconn_setting_reset(reconn_setting_t* reconn) {
    reconn->cur_delay = 0;
    reconn->cur_retry_cnt = 0;
}

HV_INLINE bool reconn_setting_can_retry(reconn_setting_t* reconn) {
    ++reconn->cur_retry_cnt;
    return reconn->max_retry_cnt == INFINITE ||
           reconn->cur_retry_cnt <= reconn->max_retry_cnt;
}

HV_INLINE uint32_t reconn_setting_calc_delay(reconn_setting_t* reconn) {
    if (reconn->delay_policy == 0) {
        // fixed
        reconn->cur_delay = reconn->min_delay;
    } else if (reconn->delay_policy == 1) {
        // linear
        reconn->cur_delay += reconn->min_delay;
    } else {
        // exponential
        reconn->cur_delay *= reconn->delay_policy;
    }
    reconn->cur_delay = MAX(reconn->cur_delay, reconn->min_delay);
    reconn->cur_delay = MIN(reconn->cur_delay, reconn->max_delay);
    return reconn->cur_delay;
}

//-----------------LoadBalance-------------------------------------
typedef enum {
    LB_RoundRobin,
    LB_Random,
    LB_LeastConnections,
    LB_IpHash,
    LB_UrlHash,
} load_balance_e;

//-----------------rudp---------------------------------------------
#if WITH_KCP
#define WITH_RUDP 1
#endif

#if WITH_RUDP
// NOTE: hio_close_rudp is thread-safe.
HV_EXPORT int hio_close_rudp(hio_t* io, struct sockaddr* peeraddr DEFAULT(NULL));
#endif

#if WITH_KCP
typedef struct kcp_setting_s {
    // ikcp_create(conv, ...)
    unsigned int conv;
    // ikcp_nodelay(kcp, nodelay, interval, fastresend, nocwnd)
    int nodelay;
    int interval;
    int fastresend;
    int nocwnd;
    // ikcp_wndsize(kcp, sndwnd, rcvwnd)
    int sndwnd;
    int rcvwnd;
    // ikcp_setmtu(kcp, mtu)
    int mtu;
    // ikcp_update
    int update_interval;

#ifdef __cplusplus
    kcp_setting_s() {
        conv = 0x11223344;
        // normal mode
        nodelay = 0;
        interval = 40;
        fastresend = 0;
        nocwnd = 0;
        // fast mode
        // nodelay = 1;
        // interval = 10;
        // fastresend = 2;
        // nocwnd = 1;

        sndwnd = 0;
        rcvwnd = 0;
        mtu = 1400;
        update_interval = 10; // ms
    }
#endif
} kcp_setting_t;

HV_INLINE void kcp_setting_init_with_normal_mode(kcp_setting_t* setting) {
    memset(setting, 0, sizeof(kcp_setting_t));
    setting->nodelay = 0;
    setting->interval = 40;
    setting->fastresend = 0;
    setting->nocwnd = 0;
}

HV_INLINE void kcp_setting_init_with_fast_mode(kcp_setting_t* setting) {
    memset(setting, 0, sizeof(kcp_setting_t));
    setting->nodelay = 0;
    setting->interval = 30;
    setting->fastresend = 2;
    setting->nocwnd = 1;
}

HV_INLINE void kcp_setting_init_with_fast2_mode(kcp_setting_t* setting) {
    memset(setting, 0, sizeof(kcp_setting_t));
    setting->nodelay = 1;
    setting->interval = 20;
    setting->fastresend = 2;
    setting->nocwnd = 1;
}

HV_INLINE void kcp_setting_init_with_fast3_mode(kcp_setting_t* setting) {
    memset(setting, 0, sizeof(kcp_setting_t));
    setting->nodelay = 1;
    setting->interval = 10;
    setting->fastresend = 2;
    setting->nocwnd = 1;
}

// @see examples/udp_echo_server.c => #define TEST_KCP 1
HV_EXPORT int hio_set_kcp(hio_t* io, kcp_setting_t* setting DEFAULT(NULL));
#endif

END_EXTERN_C

#endif // HV_LOOP_H_
