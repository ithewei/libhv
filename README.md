[![Build Status](https://travis-ci.org/ithewei/libhv.svg?branch=master)](https://travis-ci.org/ithewei/libhv)

## Intro

Like `libevent, libev, and libuv`,
`libhv` provides event-loop with non-blocking IO and timer,
but simpler apis and richer protocols.

## Features

- cross-platform (Linux, Windows, Mac, Solaris)
- event-loop (IO, timer, idle)
- ENABLE_IPV6
- ENABLE_UDS (Unix Domain Socket)
- WITH_OPENSSL
- http client/server (include https http1/x http2 grpc)
- http web service, indexof service, api service (support RESTful API)
- protocols
    - dns
    - ftp
    - smtp
- apps
    - ls,mkdir_p,rmdir_p
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
see `examples/httpd/httpd.cpp`
```c++
#include "HttpServer.h"

int main() {
    HttpService service;
    service.base_url = "/v1/api";
    service.POST("/echo", [](HttpRequest* req, HttpResponse* res) {
        res->body = req->body;
        return 200;
    });

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
make tcp udp nc
bin/tcp 1111
bin/nc 127.0.0.1 1111

bin/udp 2222
bin/nc -u 127.0.0.1 2222

make hloop_test
bin/hloop_test
bin/nc 127.0.0.1 10514
```

## BUILD

### lib
- make libhv
- sudo make install

### examples
- make examples
    - make tcp   # tcp server
    - make udp   # udp server
    - make nc    # network client
    - make nmap  # host discovery
    - make httpd # http server
    - make curl  # http client

### unittest
- make unittest

### compile options

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
sudo apt install openssl libssl-dev # ubuntu
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
sudo apt install libnghttp2-dev # ubuntu
make clean
make libhv httpd curl WITH_NGHTTP2=yes
bin/httpd -d
bin/curl -v localhost:8080 --http2
```

#### other options
see config.mk

### echo-servers
```shell
# ubuntu16.04
sudo apt install libevent-dev libev-dev libuv1-dev libboost-dev libboost-system-dev libasio-dev libpoco-dev
# muduo install => https://github.com/chenshuo/muduo.git
cd echo-servers
./build.sh
./benchmark.sh
```

**echo-servers/benchmark**<br>
![echo-servers](html/downloads/echo-servers.png)

