[![Build Status](https://travis-ci.org/ithewei/hw.svg?branch=master)](https://travis-ci.org/ithewei/hw)

## Intro

hw 是一套跨平台c/c++基础组件，函数名/类名以h/H开头

## OS (passed)

- Linux
- Windows
- Mac

## Compiler (passed)

- gcc
- clang
- msvc

## Required

- c++11

## Getting Started
```shell
git clone https://github.com/ithewei/hw.git
cd hw
make httpd curl

bin/httpd -d
ps aux | grep httpd

# http web service
bin/curl -v localhost:8080

# indexof
bin/curl -v localhost:8080/downloads/

# http api service
bin/curl -v -X POST localhost:8080/v1/api/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
```

## Module

### data-structure
- array.h:       动态数组
- list.h:        链表
- queue.h:       队列
- heap.h:        堆

### base
- hplatform.h:   平台相关宏
- hdef.h:        宏定义
- hversion.h:    版本
- hbase.h:       基本接口
- hsysinfo.h:    系统信息
- hproc.h:       子进程/线程类
- hmath.h:       math扩展函数
- htime.h:       时间
- herr.h:        错误码
- hlog.h:        日志
- hsocket.h:     socket操作
- hstring.h:     字符串
- hvar.h:        var变量
- hobj.h:        对象基类
- hgui.h:        gui相关定义
- hbuf.h:        缓存类
- hfile.h:       文件类
- hdir.h:        ls实现
- hscope.h:      作用域RAII机制
- hmutex.h：     同步锁
- hthread.h：    线程
- hthreadpool.h：线程池

### utils
- hmain.h:       main_ctx: arg env
- hendian.h:     大小端
- ifconfig.h:    ifconfig实现
- iniparser.h:   ini解析
- singleton.h:   单例模式

### event
- hloop.h:       事件循环

#### iowatcher
- EVENT_SELECT
- EVENT_POLL
- EVENT_EPOLL   (linux only)
- EVENT_KQUEUE  (mac/bsd)
- EVENT_IOCP    (windows only)

### http
- http_client.h: http客户端
- HttpServer.h:  http服务端

### other

- h.h：          总头文件
- Makefile.in:   通用Makefile模板
- main.cpp.tmpl: 通用main.cpp模板

## BUILD

### examples

- make all
- make test:  服务端master-workers model
- make timer: 定时器测试
- make loop:  事件循环(包含timer、io、idle)
- make tcp:   tcp server
- make udp:   udp server
- make nc:    network client
- make nmap:  host discovery
- make httpd: http服务(包含web service和api service)
- make curl:  基于libcurl封装http客户端

### tests
- make webbench: http服务压力测试程序
- make unittest: 单元测试

### compile options
#### compile with print debug info
- make DEFINES=PRINT_DEBUG

#### compile WITH_OPENSSL
- make DEFINES=WITH_OPENSSL

#### compile WITH_CURL
- make DEFINES="WITH_CURL CURL_STATICLIB"

#### compile WITH_NGHTTP2
- make DEFINES=WITH_NGHTTP2

#### other features
- USE_MULTIMAP
- WITH_WINDUMP
- ENABLE_IPV6
