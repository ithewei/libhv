事件循环和IO多路复用机制介绍

事件循环是`libevent、libev、libuv、libhv`这类网络库里最核心的概念，即在事件循环里处理IO读写事件、定时器事件、自定义事件等各种事件；<br>
IO多路复用即在一个IO线程监听多个fd，如最早期的`select`、后来的`poll`，`linux的epoll`、`windows的iocp`、`bsd的kqueue`、`solaris的port`等，都属于IO多路复用机制。<br>
非阻塞NIO搭配IO多路复用机制就是高并发的钥匙。<br>
`libhv`下的`event`模块正是封装了多种平台的IO多路复用机制，提供了统一的事件接口，是`libhv`的核心模块。<br>

`hloop.h`: 事件循环模块对外头文件。<br>

```c

// 事件结构体
struct hevent_s {
    hloop_t*            loop;           // 事件所属循环
    hevent_type_e       event_type;     // 事件类型
    uint64_t            event_id;       // 事件ID
    hevent_cb           cb;             // 事件回调
    void*               userdata;       // 用户数据
    void*               privdata;       // 私有数据
    struct hevent_s*    pending_next;   // 指向下一个事件，用于实现事件队列
    int                 priority;       // 事件优先级
};

// 设置事件ID
#define hevent_set_id(ev, id)           ((hevent_t*)(ev))->event_id = id
// 设置事件回调
#define hevent_set_cb(ev, cb)           ((hevent_t*)(ev))->cb = cb
// 设置事件优先级
#define hevent_set_priority(ev, prio)   ((hevent_t*)(ev))->priority = prio
// 设置事件用户数据
#define hevent_set_userdata(ev, udata)  ((hevent_t*)(ev))->userdata = (void*)udata

// 获取事件所属循环
#define hevent_loop(ev)         (((hevent_t*)(ev))->loop)
// 获取事件类型
#define hevent_type(ev)         (((hevent_t*)(ev))->event_type)
// 获取事件ID
#define hevent_id(ev)           (((hevent_t*)(ev))->event_id)
// 获取事件回调
#define hevent_cb(ev)           (((hevent_t*)(ev))->cb)
// 获取事件优先级
#define hevent_priority(ev)     (((hevent_t*)(ev))->priority)
// 获取事件用户数据
#define hevent_userdata(ev)     (((hevent_t*)(ev))->userdata)

// hidle_t、htimer_t、hio_t皆是继承自hevent_t，继承上面的数据成员和函数方法

// 新建事件循环
hloop_t* hloop_new(int flags DEFAULT(HLOOP_FLAG_AUTO_FREE));

// 释放事件循环
void hloop_free(hloop_t** pp);

// 运行事件循环
int hloop_run(hloop_t* loop);

// 停止事件循环
int hloop_stop(hloop_t* loop);

// 暂停事件循环
int hloop_pause(hloop_t* loop);

// 继续事件循环
int hloop_resume(hloop_t* loop);

// 唤醒事件循环
int hloop_wakeup(hloop_t* loop);

// 返回事件循环状态
hloop_status_e hloop_status(hloop_t* loop);

// 更新事件循环里的时间
void     hloop_update_time(hloop_t* loop);

// 返回事件循环里记录的时间
uint64_t hloop_now(hloop_t* loop);        // s
uint64_t hloop_now_ms(hloop_t* loop);     // ms
uint64_t hloop_now_us(hloop_t* loop);     // us

// 返回事件循环所在进程ID
long hloop_pid(hloop_t* loop);

// 返回事件循环所在线程ID
long hloop_tid(hloop_t* loop);

// 返回事件循环的循环次数
uint64_t hloop_count(hloop_t* loop);

// 返回事件循环里激活的IO事件数量
uint32_t hloop_nios(hloop_t* loop);

// 返回事件循环里激活的定时器事件数量
uint32_t hloop_ntimers(hloop_t* loop);

// 返回事件循环里激活的空闲事件数量
uint32_t hloop_nidles(hloop_t* loop);

// 返回事件循环里激活的事件数量
uint32_t hloop_nactives(hloop_t* loop);

// 设置事件循环的用户数据
void  hloop_set_userdata(hloop_t* loop, void* userdata);

// 获取事件循环的用户数据
void* hloop_userdata(hloop_t* loop);

// 投递事件
void hloop_post_event(hloop_t* loop, hevent_t* ev);

// 添加空闲事件
hidle_t* hidle_add(hloop_t* loop, hidle_cb cb, uint32_t repeat DEFAULT(INFINITE));

// 删除空闲事件
void     hidle_del(hidle_t* idle);

// 添加超时定时器
htimer_t* htimer_add(hloop_t* loop, htimer_cb cb, uint32_t timeout_ms, uint32_t repeat DEFAULT(INFINITE));

// 添加时间定时器
htimer_t* htimer_add_period(hloop_t* loop, htimer_cb cb,
                        int8_t minute DEFAULT(0),  int8_t hour  DEFAULT(-1), int8_t day DEFAULT(-1),
                        int8_t week   DEFAULT(-1), int8_t month DEFAULT(-1), uint32_t repeat DEFAULT(INFINITE));

// 删除定时器
void htimer_del(htimer_t* timer);

// 重置定时器
void htimer_reset(htimer_t* timer, uint32_t timeout_ms DEFAULT(0));

// 返回IO多路复用引擎 (select、poll、epoll、etc.)
const char* hio_engine();

// 获取IO对象
hio_t* hio_get(hloop_t* loop, int fd);

// 添加IO读写事件
int    hio_add(hio_t* io, hio_cb cb, int events DEFAULT(HV_READ));

// 删除IO读写事件
int    hio_del(hio_t* io, int events DEFAULT(HV_RDWR));

// 将IO对象从当前所属事件循环中剥离
void hio_detach(/*hloop_t* loop,*/ hio_t* io);

// 将IO对象关联到新的事件循环
void hio_attach(hloop_t* loop, hio_t* io);

// hio_detach 和 hio_attach 的示例代码见 examples/multi-thread/one-acceptor-multi-workers.c
/*
void new_conn_event(hevent_t* ev) {
    hloop_t* loop = ev->loop;
    hio_t* io = (hio_t*)hevent_userdata(ev);
    // 关联到新的worker事件循环
    hio_attach(loop, io);
}

void on_accpet(hio_t* io) {
    // 从acceptor所在事件循环中剥离
    hio_detach(io);

    // 将新的连接按照负载均衡策略分发到worker线程
    hloop_t* worker_loop = get_one_loop();
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.loop = worker_loop;
    ev.cb = new_conn_event;
    ev.userdata = io;
    hloop_post_event(worker_loop, &ev);
}
*/

// 判断fd是否存在于事件循环
bool hio_exists(hloop_t* loop, int fd);

// 返回一个唯一标示ID
uint32_t hio_id (hio_t* io);

// 返回文件描述符
int hio_fd      (hio_t* io);

// 返回错误码
int hio_error   (hio_t* io);

// 返回添加的事件
int hio_events  (hio_t* io);

// 获取返回的事件
int hio_revents (hio_t* io);

// 返回IO类型
hio_type_e       hio_type     (hio_t* io);

// 返回本地地址
struct sockaddr* hio_localaddr(hio_t* io);

// 返回对端地址
struct sockaddr* hio_peeraddr (hio_t* io);

// 设置上下文
void hio_set_context(hio_t* io, void* ctx);

// 获取上下文
void* hio_context(hio_t* io);

// 是否已打开
bool hio_is_opened(hio_t* io);

// 是否已连接
bool hio_is_connected(hio_t* io);

// 是否已关闭
bool hio_is_closed(hio_t* io);

// 设置读缓存
void hio_set_readbuf(hio_t* io, void* buf, size_t len);

// 获取读缓存
hio_readbuf_t* hio_get_readbuf(hio_t* io);

// 设置最大读缓存
void hio_set_max_read_bufsize (hio_t* io, uint32_t size);

// 设置最大写缓存
void hio_set_max_write_bufsize(hio_t* io, uint32_t size);

// 获取当前写缓存大小
size_t   hio_write_bufsize(hio_t* io);

// 判断是否写完成
#define hio_write_is_complete(io) (hio_write_bufsize(io) == 0)

// 获取最后读的时间
uint64_t hio_last_read_time(hio_t* io);   // ms

// 获取最后写的时间
uint64_t hio_last_write_time(hio_t* io);  // ms

// 设置accept回调
void hio_setcb_accept   (hio_t* io, haccept_cb  accept_cb);
// 设置连接回调
void hio_setcb_connect  (hio_t* io, hconnect_cb connect_cb);
// 设置读回调
void hio_setcb_read     (hio_t* io, hread_cb    read_cb);
// 设置写回调
void hio_setcb_write    (hio_t* io, hwrite_cb   write_cb);
// 设置关闭回调
void hio_setcb_close    (hio_t* io, hclose_cb   close_cb);

// 获取accept回调
haccept_cb  hio_getcb_accept(hio_t* io);
// 获取连接回调
hconnect_cb hio_getcb_connect(hio_t* io);
// 获取读回调
hread_cb    hio_getcb_read(hio_t* io);
// 获取写回调
hwrite_cb   hio_getcb_write(hio_t* io);
// 获取关闭回调
hclose_cb   hio_getcb_close(hio_t* io);

// 开启SSL/TLS加密通信
int  hio_enable_ssl(hio_t* io);
// 是否SSL/TLS加密通信
bool hio_is_ssl(hio_t* io);
// 设置SSL
int  hio_set_ssl    (hio_t* io, hssl_t ssl);
// 设置SSL_CTX
int  hio_set_ssl_ctx(hio_t* io, hssl_ctx_t ssl_ctx);
// 新建SSL_CTX
int  hio_new_ssl_ctx(hio_t* io, hssl_ctx_opt_t* opt);
// 获取SSL
hssl_t     hio_get_ssl(hio_t* io);
// 获取SSL_CTX
hssl_ctx_t hio_get_ssl_ctx(hio_t* io);
// 设置主机名
int         hio_set_hostname(hio_t* io, const char* hostname);
// 获取主机名
const char* hio_get_hostname(hio_t* io);

// 设置连接超时
void hio_set_connect_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_CONNECT_TIMEOUT));
// 设置关闭超时 (说明：非阻塞写队列非空时，需要等待写完成再关闭)
void hio_set_close_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_CLOSE_TIMEOUT));
// 设置读超时 (一段时间没有数据到来便自动关闭连接)
void hio_set_read_timeout(hio_t* io, int timeout_ms);
// 设置写超时 (一段时间没有数据发送便自动关闭连接)
void hio_set_write_timeout(hio_t* io, int timeout_ms);
// 设置keepalive超时 (一段时间没有数据收发便自动关闭连接)
void hio_set_keepalive_timeout(hio_t* io, int timeout_ms DEFAULT(HIO_DEFAULT_KEEPALIVE_TIMEOUT));

// 设置心跳 (定时发送心跳包)
typedef void (*hio_send_heartbeat_fn)(hio_t* io);
void hio_set_heartbeat(hio_t* io, int interval_ms, hio_send_heartbeat_fn fn);

// 接收连接
// hio_add(io, HV_READ) => accept => haccept_cb
int hio_accept (hio_t* io);

// 连接
// connect => hio_add(io, HV_WRITE) => hconnect_cb
int hio_connect(hio_t* io);

// 读
// hio_add(io, HV_READ) => read => hread_cb
int hio_read   (hio_t* io);

// 开始读
#define hio_read_start(io) hio_read(io)

// 停止读
#define hio_read_stop(io)  hio_del(io, HV_READ)

// 读一次
// hio_read_start => hread_cb => hio_read_stop
int hio_read_once (hio_t* io);

// 读取直到指定长度
// hio_read_once => hread_cb(len)
int hio_read_until_length(hio_t* io, unsigned int len);

// 读取直到遇到分隔符
// hio_read_once => hread_cb(...delim)
int hio_read_until_delim (hio_t* io, unsigned char delim);

// 读取一行
#define hio_readline(io)        hio_read_until_delim(io, '\n')

// 读取字符串
#define hio_readstring(io)      hio_read_until_delim(io, '\0')

// 读取N个字节
#define hio_readbytes(io, len)  hio_read_until_length(io, len)
#define hio_read_until(io, len) hio_read_until_length(io, len)

// 写
// hio_try_write => hio_add(io, HV_WRITE) => write => hwrite_cb
int hio_write  (hio_t* io, const void* buf, size_t len);

// 关闭
// hio_del(io, HV_RDWR) => close => hclose_cb
int hio_close  (hio_t* io);

// 异步关闭 (投递一个close事件)
// NOTE: hloop_post_event(hio_close_event)
int hio_close_async(hio_t* io);

//------------------高等级的接口-------------------------------------------
// 读
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb);
// 写
// hio_get -> hio_setcb_write -> hio_write
hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));
// 关闭
// hio_get -> hio_close
void   hclose   (hloop_t* loop, int fd);

// tcp
// 接收连接
// hio_get -> hio_setcb_accept -> hio_accept
hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb);
// 连接
// hio_get -> hio_setcb_connect -> hio_connect
hio_t* hconnect (hloop_t* loop, int connfd,   hconnect_cb connect_cb);
// 接收
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
hio_t* hrecv    (hloop_t* loop, int connfd, void* buf, size_t len, hread_cb read_cb);
// 发送
// hio_get -> hio_setcb_write -> hio_write
hio_t* hsend    (hloop_t* loop, int connfd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));

// udp
// 设置IO类型
void hio_set_type(hio_t* io, hio_type_e type);
// 设置本地地址
void hio_set_localaddr(hio_t* io, struct sockaddr* addr, int addrlen);
// 设置对端地址
void hio_set_peeraddr (hio_t* io, struct sockaddr* addr, int addrlen);
// 接收
// hio_get -> hio_set_readbuf -> hio_setcb_read -> hio_read
hio_t* hrecvfrom (hloop_t* loop, int sockfd, void* buf, size_t len, hread_cb read_cb);
// 发送
// hio_get -> hio_setcb_write -> hio_write
hio_t* hsendto   (hloop_t* loop, int sockfd, const void* buf, size_t len, hwrite_cb write_cb DEFAULT(NULL));

//-----------------顶层的接口---------------------------------------------
// 创建socket套接字，返回IO对象
// @hio_create_socket: socket -> bind -> listen
// sockaddr_set_ipport -> socket -> hio_get(loop, sockfd) ->
// side == HIO_SERVER_SIDE ? bind ->
// type & HIO_TYPE_SOCK_STREAM ? listen ->
hio_t* hio_create_socket(hloop_t* loop, const char* host, int port,
                            hio_type_e type DEFAULT(HIO_TYPE_TCP),
                            hio_side_e side DEFAULT(HIO_SERVER_SIDE));

// @tcp_server: hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_SERVER_SIDE) -> hio_setcb_accept -> hio_accept
// 创建TCP服务，示例代码见 examples/tcp_echo_server.c
hio_t* hloop_create_tcp_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);

// @tcp_client: hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE) -> hio_setcb_connect -> hio_setcb_close -> hio_connect
// 创建TCP客户端，示例代码见 examples/nc.c
hio_t* hloop_create_tcp_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb, hclose_cb close_cb);

// @ssl_server: hio_create_socket(loop, host, port, HIO_TYPE_SSL, HIO_SERVER_SIDE) -> hio_setcb_accept -> hio_accept
// 创建SSL服务端，示例代码见 examples/tcp_echo_server.c => #define TEST_SSL 1
hio_t* hloop_create_ssl_server (hloop_t* loop, const char* host, int port, haccept_cb accept_cb);

// @ssl_client: hio_create_socket(loop, host, port, HIO_TYPE_SSL, HIO_CLIENT_SIDE) -> hio_setcb_connect -> hio_setcb_close -> hio_connect
// 创建SSL客户端，示例代码见 examples/nc.c => #define TEST_SSL 1
hio_t* hloop_create_ssl_client (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb, hclose_cb close_cb);

// @udp_server: hio_create_socket(loop, host, port, HIO_TYPE_UDP, HIO_SERVER_SIDE)
// 创建UDP服务端，示例代码见 examples/udp_echo_server.c
hio_t* hloop_create_udp_server (hloop_t* loop, const char* host, int port);

// @udp_server: hio_create_socket(loop, host, port, HIO_TYPE_UDP, HIO_CLIENT_SIDE)
// 创建UDP客户端，示例代码见 examples/nc.c
hio_t* hloop_create_udp_client (hloop_t* loop, const char* host, int port);

//-----------------转发---------------------------------------------
// hio_read(io)
// hio_read(io->upstream_io)
void   hio_read_upstream(hio_t* io);
// on_write(io) -> hio_write_is_complete(io) -> hio_read(io->upstream_io)
void   hio_read_upstream_on_write_complete(hio_t* io, const void* buf, int writebytes);
// hio_write(io->upstream_io, buf, bytes)
void   hio_write_upstream(hio_t* io, void* buf, int bytes);
// hio_close(io->upstream_io)
void   hio_close_upstream(hio_t* io);

// io1->upstream_io = io2;
// io2->upstream_io = io1;
// 建立转发，示例代码见 examples/socks5_proxy_server.c
void   hio_setup_upstream(hio_t* io1, hio_t* io2);

// @return io->upstream_io
hio_t* hio_get_upstream(hio_t* io);

// @tcp_upstream: hio_create_socket -> hio_setup_upstream -> hio_connect -> on_connect -> hio_read_upstream
// @return upstream_io
// 建立TCP转发，示例代码见 examples/tcp_proxy_server.c
hio_t* hio_setup_tcp_upstream(hio_t* io, const char* host, int port, int ssl DEFAULT(0));
// 建立SSL转发
#define hio_setup_ssl_upstream(io, host, port) hio_setup_tcp_upstream(io, host, port, 1)

// @udp_upstream: hio_create_socket -> hio_setup_upstream -> hio_read_upstream
// @return upstream_io
// 建立UDP转发，示例代码见 examples/udp_proxy_server.c
hio_t* hio_setup_udp_upstream(hio_t* io, const char* host, int port);

//-----------------拆包---------------------------------------------
// 拆包模式
typedef enum {
    UNPACK_MODE_NONE        = 0,
    UNPACK_BY_FIXED_LENGTH  = 1,    // 固定长度拆包，不建议
    UNPACK_BY_DELIMITER     = 2,    // 根据分隔符拆包，适用于文本协议
    UNPACK_BY_LENGTH_FIELD  = 3,    // 根据头部长度字段拆包，适用于二进制协议
} unpack_mode_e;

// 拆包设置
typedef struct unpack_setting_s {
    unpack_mode_e   mode;               // 拆包模式
    unsigned int    package_max_length; // 最大的包长
    union {
        // UNPACK_BY_FIXED_LENGTH: 固定长度拆包设置
        struct {
            unsigned int    fixed_length; // 固定长度
        };
        // UNPACK_BY_DELIMITER: 分隔符拆包设置
        struct {
            unsigned char   delimiter[PACKAGE_MAX_DELIMITER_BYTES]; // 分隔符
            unsigned short  delimiter_bytes;                        // 分隔符所占字节数
        };
        /*
         * UNPACK_BY_LENGTH_FIELD: 头部长度字段拆包设置
         *
         * 包长        = 头部长度 + 数据长度 + 调整长度
         * package_len = head_len + body_len + length_adjustment
         *
         * if (length_field_coding == ENCODE_BY_VARINT) head_len = body_offset + varint_bytes - length_field_bytes;
         * else head_len = body_offset;
         *
         * 注意：头部长度字段的值仅代表数据长度，不包括头部本身长度，
         * 如果你的头部长度字段代表总包长，那么应该将length_adjustment设置为负的头部长度
         * length_field stores body length, exclude head length,
         * if length_field = head_len + body_len, then length_adjustment should be set to -head_len.
         *
         */
        struct {
            unsigned short  body_offset; // 到数据的偏移，通常等于头部长度
            unsigned short  length_field_offset; // 长度字段偏移
            unsigned short  length_field_bytes;  // 长度字段所占字节数
                     short  length_adjustment;   // 调整长度
            unpack_coding_e length_field_coding; // 长度字段编码方式
        };
    };
} unpack_setting_t;

/*
 * 拆包示例代码见 examples/jsonrpc examples/protorpc
 *
 * 注意：多个IO对象的unpack_setting_t可能是一样的，所有hio_t里仅保存了unpack_setting_t的指针，
 *       unpack_setting_t的生命周期应该被调用者所保证，不应该使用局部变量。
 */

// 设置拆包
void hio_set_unpack(hio_t* io, unpack_setting_t* setting);
// 取消拆包设置
void hio_unset_unpack(hio_t* io);

// 拆包设置示例：
/*

// FTP协议通过\r\n分割符拆包
unpack_setting_t ftp_unpack_setting;
memset(&ftp_unpack_setting, 0, sizeof(unpack_setting_t));
ftp_unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
ftp_unpack_setting.mode = UNPACK_BY_DELIMITER;
ftp_unpack_setting.delimiter[0] = '\r';
ftp_unpack_setting.delimiter[1] = '\n';
ftp_unpack_setting.delimiter_bytes = 2;

// MQTT协议通过头部长度字段拆包，头部长度字段使用了varint编码
unpack_setting_t mqtt_unpack_setting = {
    .mode = UNPACK_BY_LENGTH_FIELD,
    .package_max_length = DEFAULT_PACKAGE_MAX_LENGTH,
    .body_offset = 2,
    .length_field_offset = 1,
    .length_field_bytes = 1,
    .length_field_coding = ENCODE_BY_VARINT,
};

*/

//-----------------重连----------------------------------------
// 重连设置
typedef struct reconn_setting_s {
    uint32_t min_delay;  // ms      重连最小延时
    uint32_t max_delay;  // ms      重连最大延时
    uint32_t cur_delay;  // ms      当前延时
    /*
     * @delay_policy: 延时策略
     * 0: fixed 固定延时
     * min_delay=3s => 3,3,3...
     * 1: linear 线性增长延时
     * min_delay=3s max_delay=10s => 3,6,9,10,10...
     * other: exponential 指数增长延时
     * min_delay=3s max_delay=60s delay_policy=2 => 3,6,12,24,48,60,60...
     */
    uint32_t delay_policy;  // 延时策略
    uint32_t max_retry_cnt; // 最大重试次数
    uint32_t cur_retry_cnt; // 当前重试次数
} reconn_setting_t;

// 重连设置初始化
void reconn_setting_init(reconn_setting_t* reconn);

// 重连设置重置
void reconn_setting_reset(reconn_setting_t* reconn);

// 增加重试次数并判断是否未超过最大重试次数
bool reconn_setting_can_retry(reconn_setting_t* reconn);

// 计算当前重连延时
uint32_t reconn_setting_calc_delay(reconn_setting_t* reconn);

//-----------------负载均衡-------------------------------------
// 负载均衡策略枚举
typedef enum {
    LB_RoundRobin,      // 轮询
    LB_Random,          // 随机
    LB_LeastConnections,// 最少连接数
    LB_IpHash,          // IP hash
    LB_UrlHash,         // URL hash
} load_balance_e;

//-----------------可靠UDP---------------------------------------------
// 关闭可靠UDP
int hio_close_rudp(hio_t* io, struct sockaddr* peeraddr DEFAULT(NULL));

// KCP设置
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
} kcp_setting_t;

// KCP 正常模式
HV_INLINE void kcp_setting_init_with_normal_mode(kcp_setting_t* setting);

// KCP fast模式
void kcp_setting_init_with_fast_mode(kcp_setting_t* setting);

// KCP fast2模式
void kcp_setting_init_with_fast2_mode(kcp_setting_t* setting);

// KCP fast3模式
void kcp_setting_init_with_fast3_mode(kcp_setting_t* setting);

// 设置KCP，示例代码见 examples/udp_echo_server.c => #define TEST_KCP 1
int hio_set_kcp(hio_t* io, kcp_setting_t* setting DEFAULT(NULL));

```

示例代码：

- 事件循环:     [examples/hloop_test.c](../../examples/hloop_test.c)
- 定时器:       [examples/htimer_test.c](../../examples/htimer_test.c)
- TCP回显服务:  [examples/tcp_echo_server.c](../../examples/tcp_echo_server.c)
- TCP聊天服务:  [examples/tcp_chat_server.c](../../examples/tcp_chat_server.c)
- TCP代理服务:  [examples/tcp_proxy_server.c](../../examples/tcp_proxy_server.c)
- TCP客户端:    [examples/tcp_client_test.c](../../examples/tcp_client_test.c)
- UDP回显服务:  [examples/udp_echo_server.c](../../examples/udp_echo_server.c)
- UDP代理服务:  [examples/udp_proxy_server.c](../../examples/udp_proxy_server.c)
- 网络客户端:   [examples/nc](../../examples/nc.c)
- SOCKS5代理服务: [examples/socks5_proxy_server.c](../../examples/socks5_proxy_server.c)
- HTTP服务:     [examples/tinyhttpd.c](../../examples/tinyhttpd.c)
- HTTP代理服务: [examples/tinyproxyd.c](../../examples/tinyproxyd.c)
- jsonRPC示例:  [examples/jsonrpc](../../examples/jsonrpc)
- protobufRPC示例: [examples/protorpc](../../examples/protorpc)

多进程/多线程模式示例代码：

- 多accept进程模式: [examples/multi-thread/multi-acceptor-processes.c](../../examples/multi-thread/multi-acceptor-processes.c)
- 多accept线程模式: [examples/multi-thread/multi-acceptor-threads.c](../../examples/multi-thread/multi-acceptor-threads.c)
- 一个accept线程+多worker线程: [examples/multi-thread/one-acceptor-multi-workers.c](../../examples/multi-thread/one-acceptor-multi-workers.c)
