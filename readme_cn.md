[![Build Status](https://travis-ci.org/ithewei/libhv.svg?branch=master)](https://travis-ci.org/ithewei/libhv)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows%20%7C%20Mac-blue)](.travis.yml)

[English version](README.md)

## 简介

`libhv`是一个类似于`libevent、libev、libuv`的跨平台网络库，提供了更简单的接口和更丰富的协议。

## 特征

- cross-platform (Linux, Windows, Mac)
- event-loop (IO, timer, idle)
- ENABLE_IPV6
- ENABLE_UDS (Unix Domain Socket)
- WITH_OPENSSL or WITH_MBEDTLS
- http client/server (include https http1/x http2 grpc)
- http web service, indexof service, api service (support RESTful API)
- protocols
    - dns
    - ftp
    - smtp
- apps
    - ifconfig
    - ping
    - nc
    - nmap
    - nslookup
    - ftp
    - sendmail
    - httpd
    - curl

## 入门
```
./getting_started.sh
```

### HTTP
#### http server
见[examples/http_server_test.cpp](examples/http_server_test.cpp)
```c++
#include "HttpServer.h"

int main() {
    HttpService router;
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    router.GET("/data", [](HttpRequest* req, HttpResponse* resp) {
        static char data[] = "0123456789";
        return resp->Data(data, 10);
    });

    router.GET("/paths", [&router](HttpRequest* req, HttpResponse* resp) {
        return resp->Json(router.Paths());
    });

    router.POST("/echo", [](HttpRequest* req, HttpResponse* resp) {
        resp->content_type = req->content_type;
        resp->body = req->body;
        return 200;
    });

    http_server_t server;
    server.port = 8080;
    server.service = &router;
    http_server_run(&server);
    return 0;
}
```
#### http client
见[examples/http_client_test.cpp](examples/http_client_test.cpp)
```c++
#include "requests.h"

int main() {
    auto resp = requests::get("http://www.example.com");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    resp = requests::post("127.0.0.1:8080/echo", "hello,world!");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    return 0;
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
bin/curl -v localhost:8080/ping
bin/curl -v localhost:8080/echo -d "hello,world!"
bin/curl -v localhost:8080/query?page_no=1\&page_size=10
bin/curl -v localhost:8080/kv   -H "Content-Type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
bin/curl -v localhost:8080/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
bin/curl -v localhost:8080/form -F "user=admin pswd=123456"
bin/curl -v localhost:8080/upload -F "file=@LICENSE"

bin/curl -v localhost:8080/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'
bin/curl -v localhost:8080/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'
bin/curl -v localhost:8080/test -F 'bool=1 int=123 float=3.14 string=hello'
# RESTful API: /group/:group_name/user/:user_id
bin/curl -v -X DELETE localhost:8080/group/test/user/123

# webbench (linux only)
make webbench
bin/webbench -c 2 -t 60 localhost:8080
```

**libhv(port:8080) vs nginx(port:80)**
![libhv-vs-nginx.png](html/downloads/libhv-vs-nginx.png)

### EventLoop
见[examples/tcp_echo_server.c](examples/tcp_echo_server.c) [examples/udp_echo_server.c](examples/udp_echo_server.c) [examples/nc.c](examples/nc.c)
```c++
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
    hio_t* listenio = hloop_create_tcp_server(loop, "0.0.0.0", port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
```
```shell
make examples

bin/tcp_echo_server 1234
bin/nc 127.0.0.1 1234

bin/tcp_chat_server 1234
bin/nc 127.0.0.1 1234
bin/nc 127.0.0.1 1234

bin/httpd -s restart -d
bin/tcp_proxy_server 1234 127.0.0.1:8080
bin/curl -v 127.0.0.1:8080
bin/curl -v 127.0.0.1:1234

bin/udp_echo_server 1234
bin/nc -u 127.0.0.1 1234
```

## 构建
见[BUILD.md](BUILD.md)

### 库
- make libhv
- sudo make install

### 示例
- make examples

### 单元测试
- make unittest

### 编译选项

#### 编译WITH_OPENSSL
在libhv中开启SSL非常简单，仅需要两个API接口：
```
// init ssl_ctx, see base/hssl.h
hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param);

// enable ssl, see event/hloop.h
int hio_enable_ssl(hio_t* io);
```

https就是做好的例子:
```
sudo apt install openssl libssl-dev # ubuntu
make clean
make WITH_OPENSSL=yes
# editor etc/httpd.conf => ssl = on
bin/httpd -s restart -d
bin/curl -v https://localhost:8080
curl -v https://localhost:8080 --insecure
```

#### 编译WITH_CURL
```
make WITH_CURL=yes DEFINES="CURL_STATICLIB"
```

#### 编译WITH_NGHTTP2
```
sudo apt install libnghttp2-dev # ubuntu
make clean
make WITH_NGHTTP2=yes
bin/httpd -d
bin/curl -v localhost:8080 --http2
```

#### 更多选项
见[config.mk](config.mk)

### echo-servers
```shell
cd echo-servers
./build.sh
./benchmark.sh
```

**echo-servers/benchmark**<br>
![echo-servers](html/downloads/echo-servers.png)

### 数据结构
- array.h:       动态数组
- list.h:        链表
- queue.h:       队列
- heap.h:        堆

### base
- hv.h：         总头文件
- hexport.h:     导出宏
- hplatform.h:   平台相关宏
- hdef.h:        常用宏定义
- hatomic.h:     原子操作
- herr.h:        错误码
- htime.h:       时间日期
- hmath.h:       数学函数
- hbase.h:       基本接口
- hversion.h:    版本
- hsysinfo.h:    系统信息
- hproc.h:       进程
- hthread.h：    线程
- hmutex.h：     互斥锁
- hsocket.h:     套接字
- hssl.h:        SSL/TLS加密通信
- hlog.h:        日志
- hbuf.h:        缓存
- hstring.h:     字符串
- hfile.h:       文件类
- hdir.h:        ls实现
- hurl.h:        URL相关
- hscope.h:      作用域
- hthreadpool.h: 线程池
- hobjectpool.h: 对象池
- ifconfig.h:    ifconfig实现

### utils
- hmain.h:       命令行解析
- hendian.h:     大小端
- iniparser.h:   ini解析
- singleton.h:   单例模式
- md5.h:         MD5数字摘要
- base64.h:      base64编码
- json.hpp:      json解析

### event
- hloop.h:       事件循环
- nlog.h:        网络日志
- nmap.h:        nmap实现

#### iowatcher
- EVENT_SELECT
- EVENT_POLL
- EVENT_EPOLL   (linux only)
- EVENT_KQUEUE  (mac/bsd)
- EVENT_PORT    (solaris)
- EVENT_IOCP    (windows only)

### http
- http_client.h: http客户端
- HttpServer.h:  http服务端

### protocol
- dns.h:         DNS域名查询
- icmp.h:        ping实现
- ftp.h:         FTP文件传输协议
- smtp.h:        SMTP邮件传输协议

## 学习资料

- libhv 博客专栏: <https://hewei.blog.csdn.net/category_9866493.html>
- libhv QQ群`739352073`，欢迎加群讨论
