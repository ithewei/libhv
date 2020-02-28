[![Build Status](https://travis-ci.org/ithewei/libhv.svg?branch=master)](https://travis-ci.org/ithewei/libhv)

## Intro

Like `libevent, libev, and libuv`,
`libhv` provides event-loop with non-blocking IO and timer,
but simpler apis and richer protocols.

## Features

- cross-platform (Linux, Windows, Mac)
- event-loop (IO, timer, idle)
- ENABLE_IPV6
- WITH_OPENSSL
- http client/server (include https http1/x http2 grpc)
- http web service, indexof service, api service (support RESTful API)
- protocols
    - dns
    - ftp
    - smtp
- apps
    - ls
    - ifconfig
    - ping
    - nc
    - nmap
    - nslookup
    - ftp
    - sendmail
    - httpd
    - curl

## Getting Started

### HTTP
#### http server
see `examples/httpd/httpd.cpp`
```c++
#include "HttpServer.h"

int http_api_echo(HttpRequest* req, HttpResponse* res) {
    res->body = req->body;
    return 0;
}

int main() {
    HttpService service;
    service.base_url = "/v1/api";
    service.AddApi("/echo", HTTP_POST, http_api_echo);

    http_server_t server;
    server.port = 8080;
    server.service = &service;
    http_server_run(&server);
    return 0;
}
```
#### http client
see `examples/curl.cpp`
```c++
#include "http_client.h"

int main(int argc, char* argv[]) {
    HttpRequest req;
    req.method = HTTP_POST;
    req.url = "http://localhost:8080/v1/api/echo";
    req.body = "hello,world!";
    HttpResponse res;
    int ret = http_client_send(&req, &res);
    printf("%s\n", req.Dump(true,true).c_str());
    if (ret != 0) {
        printf("* Failed:%s:%d\n", http_client_strerror(ret), ret);
    }
    else {
        printf("%s\n", res.Dump(true,true).c_str());
    }
    return ret;
}
```

```shell
git clone https://github.com/ithewei/libhv.git
cd libhv
make httpd curl

bin/httpd -h
bin/httpd -d
#bin/httpd -c etc/httpd.conf -s restart -d
ps aux | grep httpd

# http web service
bin/curl -v localhost:8080

# http indexof service
bin/curl -v localhost:8080/downloads/

# http api service
bin/curl -v localhost:8080/v1/api/hello
bin/curl -v localhost:8080/v1/api/echo -d "hello,world!"
bin/curl -v localhost:8080/v1/api/query?page_no=1\&page_size=10
bin/curl -v localhost:8080/v1/api/kv   -H "Content-Type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
bin/curl -v localhost:8080/v1/api/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
bin/curl -v localhost:8080/v1/api/form -F "file=@LICENSE"

bin/curl -v localhost:8080/v1/api/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'
bin/curl -v localhost:8080/v1/api/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'
bin/curl -v localhost:8080/v1/api/test -F 'bool=1 int=123 float=3.14 string=hello'
# RESTful API: /group/:group_name/user/:user_id
bin/curl -v -X DELETE localhost:8080/v1/api/group/test/user/123

# webbench (linux only)
make webbench
bin/webbench -c 2 -t 60 localhost:8080
```

**libhv(port:8080) vs nginx(port:80)**
![libhv-vs-nginx.png](html/downloads/libhv-vs-nginx.png)

### EventLoop
see `examples/tcp.c` `examples/udp.c`
```c
// TCP echo server
#include "hloop.h"

void on_close(hio_t* io) {
}

void on_recv(hio_t* io, void* buf, int readbytes) {
    hio_write(io, buf, readbytes);
}

void on_accept(hio_t* io) {
    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    hloop_t* loop = hloop_new(0);
    hio_t* listenio = create_tcp_server(loop, "0.0.0.0", port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
```
```shell
make tcp udp nc
bin/tcp 1111
bin/nc 127.0.0.1 1111

bin/udp 2222
bin/nc -u 127.0.0.1 2222
```

## BUILD

### lib
- make libhv
- sudo make install

### examples
- make examples
    - make test # master-workers model
    - make timer # timer add/del/reset
    - make loop # event-loop(include idle, timer, io)
    - make tcp  # tcp server
    - make udp  # udp server
    - make nc   # network client
    - make nmap # host discovery
    - make httpd # http server
    - make curl # http client

### unittest
- make unittest

### compile options
#### compile with print debug info
- make DEFINES=PRINT_DEBUG

#### compile WITH_OPENSSL
libhv combines OpenSSL perfectly, something almost all asynchronous IO network libraries don't do.<br>
And enable SSL in libhv is so easy, just only two apis:
```
// init global SSL_CTX, see base/ssl_ctx.h
int ssl_ctx_init(const char* crt_file, const char* key_file, const char* ca_file);

// enable ssl, see event/hloop.h
int hio_enable_ssl(hio_t* io);
```

https is the best example.
```
sudo apt-get install openssl libssl-dev # ubuntu
make clean
make libhv httpd curl WITH_OPENSSL=yes
# editor etc/httpd.conf => ssl = on
bin/httpd -d
bin/curl -v https://localhost:8080
curl -v https://localhost:8080 --insecure
```

#### compile WITH_CURL
- make WITH_CURL=yes DEFINES="CURL_STATICLIB"

#### compile WITH_NGHTTP2
```
sudo apt-get install libnghttp2-dev # ubuntu
make clean
make libhv httpd curl WITH_NGHTTP2=yes
bin/httpd -d
bin/curl -v localhost:8080 --http2
```

#### other options
see config.mk

### echo-servers
```shell
make libhv
make webbench

# ubuntu16.04
sudo apt-get install libevent-dev libev-dev libuv1-dev libboost-dev libasio-dev libpoco-dev
# muduo install => https://github.com/chenshuo/muduo.git
make echo-servers
sudo echo-servers/benchmark.sh
```

**echo-servers/benchmark**<br>
![echo-servers](html/downloads/echo-servers.jpg)

Note: The client and servers are located in the same computer, the results are random, for reference only.
In general, the performance of these libraries are similar, each has its own advantages.

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
- hmutex.h：     同步锁
- hthread.h：    线程
- hsocket.h:     socket操作
- hbuf.h:        缓存类
- hurl.h:        URL转义
- hgui.h:        gui相关定义
- hstring.h:     字符串
- hvar.h:        var变量
- hobj.h:        对象基类
- hfile.h:       文件类
- hdir.h:        ls实现
- hscope.h:      作用域RAII机制
- hthreadpool.h: 线程池
- hobjectpool.h: 对象池
- ifconfig.h:    ifconfig实现

### utils
- hmain.h:       main_ctx: arg env
- hendian.h:     大小端
- iniparser.h:   ini解析
- singleton.h:   单例模式
- md5.h
- base64.h
- json.hpp

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

- hv.h：         总头文件
- Makefile.in:   通用Makefile模板

## 学习资料

- libhv每日一学博客: <https://hewei.blog.csdn.net/article/details/103903123>
- libhv QQ群`739352073`，欢迎加群讨论
