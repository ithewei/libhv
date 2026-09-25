## 目录结构

```
.
├── hloop.h     事件循环模块对外头文件
├── hevent.h    事件结构体定义
├── hdns.h      异步DNS
├── nlog.h      网络日志
├── unpack.h    拆包
├── rudp.h      可靠UDP
├── proxy.c     代理
├── tls.c       TLS握手
├── socks5.c    SOCKS5代理
├── iowatcher.h IO多路复用统一抽象接口
├── select.c    EVENT_SELECT实现
├── poll.c      EVENT_POLL实现
├── epoll.c     EVENT_EPOLL实现 (for OS_LINUX/OS_WIN with wepoll)
├── wepoll/     Windows epoll兼容实现
├── io_uring.c  EVENT_IO_URING实现 (for OS_LINUX, with liburing)
├── kqueue.c    EVENT_KQUEUE实现(for OS_BSD/OS_MAC)
├── evport.c    EVENT_PORT实现  (for OS_SOLARIS)
└── nio.c       非阻塞IO

```
