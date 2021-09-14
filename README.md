English | [中文](README-CN.md)

# libhv

[![platform](https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-blue)](.github/workflows/CI.yml)
[![CI](https://github.com/ithewei/libhv/workflows/CI/badge.svg?branch=master)](https://github.com/ithewei/libhv/actions/workflows/CI.yml?query=branch%3Amaster)
[![benchmark](https://github.com/ithewei/libhv/workflows/benchmark/badge.svg?branch=master)](https://github.com/ithewei/libhv/actions/workflows/benchmark.yml?query=branch%3Amaster)
<br>
[![release](https://badgen.net/github/release/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/releases)
[![stars](https://badgen.net/github/stars/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/stargazers)
[![forks](https://badgen.net/github/forks/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/network/members)
[![issues](https://badgen.net/github/issues/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/issues)
[![PRs](https://badgen.net/github/prs/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/pulls)
[![license](https://badgen.net/github/license/ithewei/libhv?icon=github)](LICENSE)
<br>
[![gitee](https://badgen.net/badge/mirror/gitee/red)](https://gitee.com/libhv/libhv)
[![awesome-c](https://badgen.net/badge/icon/awesome-c/pink?icon=awesome&label&color)](https://github.com/oz123/awesome-c)
[![awesome-cpp](https://badgen.net/badge/icon/awesome-cpp/pink?icon=awesome&label&color)](https://github.com/fffaraz/awesome-cpp)

Like `libevent, libev, and libuv`,
`libhv` provides event-loop with non-blocking IO and timer,
but simpler api and richer protocols.

## ✨ Features

- Cross-platform (Linux, Windows, MacOS, Solaris)
- EventLoop (IO, timer, idle, custom)
- TCP/UDP client/server/proxy
- TCP supports heartbeat, upstream, unpack, MultiThread-safe write and close, etc.
- SSL/TLS support: (via WITH_OPENSSL or WITH_GNUTLS or WITH_MBEDTLS)
- HTTP client/server (support https http1/x http2 grpc)
- HTTP static file service, indexof service, sync/async API handler
- HTTP supports RESTful, URI router, keep-alive, chunked, etc.
- WebSocket client/server

## ⌛️ Build

see [BUILD.md](BUILD.md)

Makefile:
```shell
./configure
make
sudo make install
```

or cmake:
```shell
mkdir build
cd build
cmake ..
cmake --build .
```

or vcpkg:
```shell
vcpkg install libhv
```

or xmake:
```shell
xrepo install libhv
```

## ⚡️ Getting Started

run `./getting_started.sh`:

```shell
git clone https://github.com/ithewei/libhv.git
cd libhv
make

bin/httpd -h
bin/httpd -d
#bin/httpd -c etc/httpd.conf -s restart -d
ps aux | grep httpd

# http file service
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
```

### HTTP
#### http server
see [examples/http_server_test.cpp](examples/http_server_test.cpp)

**golang gin style**
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

    router.GET("/get", [](HttpRequest* req, HttpResponse* resp) {
        resp->json["origin"] = req->client_addr.ip;
        resp->json["url"] = req->url;
        resp->json["args"] = req->query_params;
        resp->json["headers"] = req->headers;
        return 200;
    });

    router.POST("/echo", [](const HttpContextPtr& ctx) {
        return ctx->send(ctx->body(), ctx->type());
    });

    http_server_t server;
    server.port = 8080;
    server.service = &router;
    http_server_run(&server);
    return 0;
}
```
#### http client
see [examples/http_client_test.cpp](examples/http_client_test.cpp)

**python requests style**
```c++
#include "requests.h"

int main() {
    auto resp = requests::get("http://www.example.com");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%s\n", resp->body.c_str());
    }

    resp = requests::post("127.0.0.1:8080/echo", "hello,world!");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%s\n", resp->body.c_str());
    }

    return 0;
}
```

**js axios style**
```c++
#include "axios.h"

int main() {
    const char* strReq = R"({
        "method": "POST",
        "url": "http://127.0.0.1:8080/echo",
        "params": {
            "page_no": "1",
            "page_size": "10"
        },
        "headers": {
            "Content-Type": "application/json"
        },
        "body": {
            "app_id": "123456",
            "app_secret": "abcdefg"
        }
    })";

    // sync
    auto resp = axios::axios(strReq);
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%s\n", resp->body.c_str());
    }

    // async
    int finished = 0;
    axios::axios(strReq, [&finished](const HttpResponsePtr& resp) {
        if (resp == NULL) {
            printf("request failed!\n");
        } else {
            printf("%s\n", resp->body.c_str());
        }
        finished = 1;
    });

    // wait async finished
    while (!finished) hv_sleep(1);
    return 0;
}
```

#### http benchmark
```shell
# sudo apt install wrk
wrk -c 100 -t 4 -d 10s http://127.0.0.1:8080/

# sudo apt install apache2-utils
ab -c 100 -n 100000 http://127.0.0.1:8080/
```

**libhv(port:8080) vs nginx(port:80)**
![libhv-vs-nginx.png](html/downloads/libhv-vs-nginx.png)

## 🍭 Examples
### c version
- [examples/hloop_test.c](examples/hloop_test.c)
- [examples/tcp_echo_server.c](examples/tcp_echo_server.c)
- [examples/tcp_chat_server.c](examples/tcp_chat_server.c)
- [examples/tcp_proxy_server.c](examples/tcp_proxy_server.c)
- [examples/udp_echo_server.c](examples/udp_echo_server.c)
- [examples/udp_proxy_server.c](examples/udp_proxy_server.c)
- [examples/jsonrpc](examples/jsonrpc)
- [examples/multi-thread/multi-acceptor-processes.c](examples/multi-thread/multi-acceptor-processes.c)
- [examples/multi-thread/multi-acceptor-threads.c](examples/multi-thread/multi-acceptor-threads.c)
- [examples/multi-thread/one-acceptor-multi-workers.c](examples/multi-thread/one-acceptor-multi-workers.c)

### c++ version
- [evpp/EventLoop_test.cpp](evpp/EventLoop_test.cpp)
- [evpp/EventLoopThread_test.cpp](evpp/EventLoopThread_test.cpp)
- [evpp/EventLoopThreadPool_test.cpp](evpp/EventLoopThreadPool_test.cpp)
- [evpp/TcpServer_test.cpp](evpp/TcpServer_test.cpp)
- [evpp/TcpClient_test.cpp](evpp/TcpClient_test.cpp)
- [evpp/UdpServer_test.cpp](evpp/UdpServer_test.cpp)
- [evpp/UdpClient_test.cpp](evpp/UdpClient_test.cpp)
- [examples/http_server_test.cpp](examples/http_server_test.cpp)
- [examples/http_client_test.cpp](examples/http_client_test.cpp)
- [examples/websocket_server_test.cpp](examples/websocket_server_test.cpp)
- [examples/websocket_client_test.cpp](examples/websocket_client_test.cpp)
- [examples/protorpc](examples/protorpc)

### simulate well-known command line tools
- [examples/nc](examples/nc.c)
- [examples/nmap](examples/nmap)
- [examples/httpd](examples/httpd)
- [examples/curl](examples/curl.cpp)
- [examples/wget](examples/wget.cpp)
- [examples/consul](examples/consul)

## 🥇 Benchmark
```shell
cd echo-servers
./build.sh
./benchmark.sh
```

**throughput**:
```shell
libevent running on port 2001
libev running on port 2002
libuv running on port 2003
libhv running on port 2004
asio running on port 2005
poco running on port 2006

==============2001=====================================
[127.0.0.1:2001] 4 threads 1000 connections run 10s
total readcount=1616761 readbytes=1655563264
throughput = 157 MB/s

==============2002=====================================
[127.0.0.1:2002] 4 threads 1000 connections run 10s
total readcount=2153171 readbytes=2204847104
throughput = 210 MB/s

==============2003=====================================
[127.0.0.1:2003] 4 threads 1000 connections run 10s
total readcount=1599727 readbytes=1638120448
throughput = 156 MB/s

==============2004=====================================
[127.0.0.1:2004] 4 threads 1000 connections run 10s
total readcount=2202271 readbytes=2255125504
throughput = 215 MB/s

==============2005=====================================
[127.0.0.1:2005] 4 threads 1000 connections run 10s
total readcount=1354230 readbytes=1386731520
throughput = 132 MB/s

==============2006=====================================
[127.0.0.1:2006] 4 threads 1000 connections run 10s
total readcount=1699652 readbytes=1740443648
throughput = 165 MB/s
```
