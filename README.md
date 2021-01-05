[![Build Status](https://travis-ci.org/ithewei/libhv.svg?branch=master)](https://travis-ci.org/ithewei/libhv)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows%20%7C%20Mac-blue)](.travis.yml)

[中文版](readme_cn.md)

## Intro

Like `libevent, libev, and libuv`,
`libhv` provides event-loop with non-blocking IO and timer,
but simpler api and richer protocols.

## Features

- cross-platform (Linux, Windows, Mac, Solaris)
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

## Getting Started
```
./getting_started.sh
```

### HTTP
#### http server
see [examples/http_server_test.cpp](examples/http_server_test.cpp)
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
see [examples/http_client_test.cpp](examples/http_client_test.cpp)
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

    auto resp = requests::post("127.0.0.1:8080/echo", "hello,world!");
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
see [examples/tcp_echo_server.c](examples/tcp_echo_server.c) [examples/udp_echo_server.c](examples/udp_echo_server.c) [examples/nc.c](examples/nc.c)
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

## BUILD
see [BUILD.md](BUILD.md)

### lib
- make libhv
- sudo make install

### examples
- make examples

### unittest
- make unittest

### compile options

#### compile WITH_OPENSSL
Enable SSL in libhv is so easy, just only two apis:
```
// init ssl_ctx, see base/hssl.h
hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param);

// enable ssl, see event/hloop.h
int hio_enable_ssl(hio_t* io);
```

https is the best example.
```
sudo apt install openssl libssl-dev # ubuntu
make clean
make WITH_OPENSSL=yes
# editor etc/httpd.conf => ssl = on
bin/httpd -s restart -d
bin/curl -v https://localhost:8080
curl -v https://localhost:8080 --insecure
```

#### compile WITH_CURL
```
make WITH_CURL=yes DEFINES="CURL_STATICLIB"
```

#### compile WITH_NGHTTP2
```
sudo apt install libnghttp2-dev # ubuntu
make clean
make WITH_NGHTTP2=yes
bin/httpd -d
bin/curl -v localhost:8080 --http2
```

#### other options
see [config.mk](config.mk)

### echo-servers
```shell
cd echo-servers
./build.sh
./benchmark.sh
```

**echo-servers/benchmark**<br>
![echo-servers](html/downloads/echo-servers.png)

