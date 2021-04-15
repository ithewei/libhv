## 目录结构

```
.
├── hloop.h     事件循环模块对外头文件
├── hevent.h    事件结构体定义
├── nlog.h      网络日志
├── iowatcher.h IO多路复用统一抽象接口
├── select.c    EVENT_SELECT实现
├── poll.c      EVENT_POLL实现
├── epoll.c     EVENT_EPOLL实现 (for OS_LINUX)
├── iocp.c      EVENT_IOCP实现  (for OS_WIN)
├── kqueue.c    EVENT_KQUEUE实现(for OS_BSD/OS_MAC)
├── evport.c    EVENT_PORT实现  (for OS_SOLARIS)
├── nio.c       非阻塞IO
└── overlapio.c 重叠IO

```
