## 目录结构

```
.
├── hloop.h     事件循环模块对外头文件
├── hevent.h    事件结构体定义
├── nlog.h      网络日志
├── unpack.h    拆包
├── rudp.h      可靠UDP
├── iowatcher.h IO多路复用统一抽象接口
├── select.c    EVENT_SELECT实现
├── poll.c      EVENT_POLL实现
├── epoll.c     EVENT_EPOLL实现 (for OS_LINUX)
├── io_uring.c  EVENT_IO_URING实现 (for OS_LINUX, with liburing)
├── iocp.c      EVENT_IOCP实现  (for OS_WIN)
├── kqueue.c    EVENT_KQUEUE实现(for OS_BSD/OS_MAC)
├── evport.c    EVENT_PORT实现  (for OS_SOLARIS)
├── nio.c       非阻塞IO
├── proxy.h/.c   通用代理上下文、HTTP CONNECT握手、TCP/UDP固定上游代理服务
├── socks5.h/.c  SOCKS5客户端/服务端握手及SOCKS5代理服务
└── overlapio.c 重叠IO

```

## Proxy

`hloop.h` 提供了基于事件循环的代理能力，统一使用 `proxy_setting_t` 描述代理监听地址、目标地址和认证信息：

```c
typedef struct proxy_setting_s {
    int  protocol;
    char proxy_host[256];
    int  proxy_port;
    char target_host[256];
    int  target_port;
    char username[256];
    char password[256];
} proxy_setting_t;
```

客户端先创建到代理地址的 socket，在调用 `hio_connect()` 前通过 `hio_set_proxy` 设置最终目标。连接建立后，IO 层会先完成代理握手，再启动 SSL 或调用用户的 `connect_cb`，后续读写对调用方透明。当前支持 SOCKS5 和 HTTP CONNECT：

```c
proxy_setting_t setting;
memset(&setting, 0, sizeof(setting));
setting.protocol = PROXY_PROTOCOL_SOCKS5;
strcpy(setting.proxy_host, "127.0.0.1");
setting.proxy_port = 1080;
strcpy(setting.target_host, "example.com");
setting.target_port = 443;

hio_t* io = hio_create_socket(loop, setting.proxy_host, setting.proxy_port,
                              HIO_TYPE_TCP, HIO_CLIENT_SIDE);
hio_set_proxy(io, &setting);
hio_setcb_connect(io, on_connect);
hio_connect(io);
```

`username` 非空时会启用认证：SOCKS5 使用 RFC 1929 用户名/密码认证，HTTP CONNECT 使用 Basic 认证。`password` 可以为空字符串。

对于 SOCKS5 客户端，可以直接使用工厂接口；它会复制配置、连接 `proxy_host:proxy_port`，并在 SOCKS5 CONNECT 成功后调用 `connect_cb`：

```c
hio_t* io = hloop_create_socks5_client(loop, &setting, on_connect, on_close);
```

服务端也使用相同的配置结构：`proxy_host/proxy_port` 是本地监听地址。TCP 和 UDP 代理把流量转发到固定的 `target_host/target_port`；SOCKS5 服务端的目标由每个客户端的 CONNECT 请求决定。

```c
hio_t* tcp_listener = hloop_create_tcp_proxy_server(loop, &setting);
hio_t* udp_listener = hloop_create_udp_proxy_server(loop, &setting);
hio_t* socks5_listener = hloop_create_socks5_proxy_server(loop, &setting);
```

可运行示例见 `examples/tcp_proxy_server.c`、`examples/udp_proxy_server.c`、`examples/socks5_proxy_server.c` 和 `examples/socks5_client_test.c`。
