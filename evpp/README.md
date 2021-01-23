The evpp module is designed to be header-only and does not participate in compilation.
hloop.h is encapsulated into c++ classes, referring to muduo and evpp.
You can modify and use evpp classes according to your own business.

evpp模块被设计成只包含头文件，不参与编译。
hloop.h中的c接口被封装成了c++的类，参考了muduo和evpp。
你能修改和使用这些类根据你自己的业务。

## 目录结构

```
.
├── Buffer.h                缓存类
├── Channel.h               通道类，封装了hio_t
├── Event.h                 事件类，封装了hevent_t、htimer_t
├── EventLoop.h             事件循环类，封装了hloop_t
├── EventLoopThread.h       事件循环线程类，组合了EventLoop和thread
├── EventLoopThreadPool.h   事件循环线程池类，组合了EventLoop和ThreadPool
├── TcpClient.h             TCP客户端类
├── TcpServer.h             TCP服务端类
├── UdpClient.h             UDP客户端类
└── UdpServer.h             UDP服务端类

```
