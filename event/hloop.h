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
    HLOOP_STATUS_STOP,
    HLOOP_STATUS_RUNNING,
    HLOOP_STATUS_PAUSE
} hloop_status_e;

typedef enum {
    HEVENT_TYPE_NONE    = 0,
    HEVENT_TYPE_IO      = 0x00000001,
    HEVENT_TYPE_TIMEOUT = 0x00000010,
    HEVENT_TYPE_PERIOD  = 0x00000020,
    HEVENT_TYPE_TIMER   = HEVENT_TYPE_TIMEOUT|HEVENT_TYPE_PERIOD,
    HEVENT_TYPE_IDLE    = 0x00000100,
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

#define HIO_DEFAULT_CONNECT_TIMEOUT     5000    // ms
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
HV_EXPORT uint64_t hloop_now(hloop_t* loop);          // s
HV_EXPORT uint64_t hloop_now_ms(hloop_t* loop);       // ms
HV_EXPORT uint64_t hloop_now_hrtime(hloop_t* loop);   // us
#define hloop_now_us hloop_now_hrtime
// @return pid of hloop_run
HV_EXPORT long hloop_pid(hloop_t* loop);
// @return tid of hloop_run
HV_EXPORT long hloop_tid(hloop_t* loop);

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

// idle
HV_EXPORT hidle_t* hidle_add(hloop_t* loop, hidle_cb cb, uint32_t repeat DEFAULT(INFINITE));
HV_EXPORT void     hidle_del(hidle_t* idle);

// timer
// @param timeout: unit(ms)
HV_EXPORT htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint32_t timeout, uint32_t repeat DEFAULT(INFINITE));
/*
 * minute   hour    day     week    month       cb
 * 0~59     0~23    1~31    0~6     1~12
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
HV_EXPORT void htimer_reset(htimer_t* timer);

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
HV_EXPORT bool hio_is_closed(hio_t* io);

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

// some useful settings
// Enable SSL/TLS is so easy :)
HV_EXPORT int  hio_enable_ssl(hio_t* io);
HV_EXPORT bool hio_is_ssl(hio_t* io);
HV_EXPORT hssl_t hio_get_ssl(hio_t* io);
HV_EXPORT int  hio_set_ssl(hio_t* io, hssl_t ssl);
// NOTE: One loop per thread, one readbuf per loop.
// But you can pass in your own readbuf instead of the default readbuf to avoid memcopy.
HV_EXPORT void hio_set_readbuf(hio_t* io, void* buf, size_t len);
// connect timeout => hclose_cb
HV_EXPORT void hio_set_connect_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_CONNECT_TIMEOUT));
// close timeout => hclose_cb
HV_EXPORT void hio_set_close_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_CLOSE_TIMEOUT));
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
HV_EXPORT int hio_read_until(hio_t* io, int len);
// NOTE: hio_write is thread-safe, locked by recursive_mutex, allow to be called by other threads.
// hio_try_write => hio_add(io, HV_WRITE) => write => hwrite_cb
HV_EXPORT int hio_write  (hio_t* io, const void* buf, size_t len);
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
// Resolver -> socket -> hio_get
HV_EXPORT hio_t* hio_create(hloop_t* loop, const char* host, int port, int type DEFAULT(SOCK_STREAM));

// @tcp_server: socket -> bind -> listen -> haccept
// @see examples/tcp_echo_server.c
HV_EXPORT hio_t* hloop_create_tcp_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);
// @tcp_client: hio_create(loop, host, port, SOCK_STREAM) -> hconnect
// @see examples/nc.c
HV_EXPORT hio_t* hloop_create_tcp_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb);

// @ssl_server: hloop_create_tcp_server -> hio_enable_ssl
// @see examples/tcp_echo_server.c => #define TEST_SSL 1
HV_EXPORT hio_t* hloop_create_ssl_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);
// @ssl_client: hio_create(loop, host, port, SOCK_STREAM) -> hio_enable_ssl -> hconnect
// @see examples/nc.c => #define TEST_SSL 1
HV_EXPORT hio_t* hloop_create_ssl_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb);

// @udp_server: socket -> bind -> hio_get
// @see examples/udp_echo_server.c
HV_EXPORT hio_t* hloop_create_udp_server (hloop_t* loop, const char* host, int port);
// @udp_client: hio_create(loop, host, port, SOCK_DGRAM)
// @see examples/nc.c
HV_EXPORT hio_t* hloop_create_udp_client (hloop_t* loop, const char* host, int port);

//-----------------upstream---------------------------------------------
// hio_read(io)
// hio_read(io->upstream_io)
HV_EXPORT void   hio_read_upstream(hio_t* io);
// hio_write(io->upstream_io, buf, bytes)
HV_EXPORT void   hio_write_upstream(hio_t* io, void* buf, int bytes);
// hio_close(io->upstream_io)
HV_EXPORT void   hio_close_upstream(hio_t* io);

// io1->upstream_io = io2;
// io2->upstream_io = io1;
// hio_setcb_read(io1, hio_write_upstream);
// hio_setcb_read(io2, hio_write_upstream);
HV_EXPORT void   hio_setup_upstream(hio_t* io1, hio_t* io2);

// @return io->upstream_io
HV_EXPORT hio_t* hio_get_upstream(hio_t* io);

// @tcp_upstream: hio_create -> hio_setup_upstream -> hio_setcb_close(hio_close_upstream) -> hconnect -> on_connect -> hio_read_upstream
// @return upstream_io
// @see examples/tcp_proxy_server
HV_EXPORT hio_t* hio_setup_tcp_upstream(hio_t* io, const char* host, int port, int ssl DEFAULT(0));
#define hio_setup_ssl_upstream(io, host, port) hio_setup_tcp_upstream(io, host, port, 1)

// @udp_upstream: hio_create -> hio_setup_upstream -> hio_read_upstream
// @return upstream_io
// @see examples/udp_proxy_server
HV_EXPORT hio_t* hio_setup_udp_upstream(hio_t* io, const char* host, int port);

//-----------------unpack---------------------------------------------
typedef enum {
    UNPACK_BY_FIXED_LENGTH  = 1,    // Not recommended
    UNPACK_BY_DELIMITER     = 2,
    UNPACK_BY_LENGTH_FIELD  = 3,    // Recommended
} unpack_mode_e;

#define DEFAULT_PACKAGE_MAX_LENGTH  (1 << 21)   // 2M

// UNPACK_BY_DELIMITER
#define PACKAGE_MAX_DELIMITER_BYTES 8

// UNPACK_BY_LENGTH_FIELD
typedef enum {
    ENCODE_BY_VARINT        = 1,
    ENCODE_BY_LITTEL_ENDIAN = LITTLE_ENDIAN,    // 1234
    ENCODE_BY_BIG_ENDIAN    = BIG_ENDIAN,       // 4321
} unpack_coding_e;

typedef struct unpack_setting_s {
    unpack_mode_e   mode;
    unsigned int    package_max_length;
    // UNPACK_BY_FIXED_LENGTH
    unsigned int    fixed_length;
    // UNPACK_BY_DELIMITER
    unsigned char   delimiter[PACKAGE_MAX_DELIMITER_BYTES];
    unsigned short  delimiter_bytes;
    // UNPACK_BY_LENGTH_FIELD
    unsigned short  body_offset; // real_body_offset = body_offset + varint_bytes - length_field_bytes
    unsigned short  length_field_offset;
    unsigned short  length_field_bytes;
    unpack_coding_e length_field_coding;
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
    }
#endif
} unpack_setting_t;

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

END_EXTERN_C

#endif // HV_LOOP_H_
