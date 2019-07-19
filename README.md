## Intro

hw 是一套跨平台c++工具集，类名以H开头

## platform

- gcc
- mingw
- msvc

## required

- c++11

## Module

### base
- hplatform.h: 平台相关
- hdef.h: 宏定义
- hversion.h: 版本
- hsysinfo.h: 系统信息
- hproc.h: 子进程/线程类
- htime.h: 时间
- herr.h: 错误码
- hlog.h: 日志
- hstring.h: 字符串
- hvar.h: var变量
- hobj.h: 对象基类
- hgui.h: gui相关定义
- hbuf.h: 缓存类
- hfile.h: 文件类
- hscope.h: 作用域RAII机制
- hmutex.h：同步锁
- hthread.h：线程
- hthreadpool.h：线程池

### utils
- hendian.h: 大小端
- hmain.h: main_ctx: arg env
- ifconfig.h: ifconfig实现
- singleton.h: 单例模式
- iniparser.h: ini解析

### event
- hloop.h: 事件循环

### http
- http_client.h: http客户端
- http_server.h: http服务端

### other

- h.h：总头文件
- Makefile.in: 通用Makefile模板
- main.cpp.tmpl: 通用main.cpp模板

## BUILD

### examples

- make all
- make test: 服务端master-workers model
- make loop: 事件循环(包含timer、io、idle)
- make client server：非阻塞socket
- make httpd: http服务（包含web service和api service），性能测试仅比nginx差一点（估计是nginx accept_mutex规避了惊群效应）
- make curl: 基于libcurl封装http客户端
- make webbench: http服务压力测试程序
