English | [中文](README-CN.md)

# libhv

[![Linux](https://badgen.net/badge/Linux/success/green?icon=github)](https://github.com/ithewei/libhv/actions/workflows/CI.yml?query=branch%3Amaster)
[![Windows](https://badgen.net/badge/Windows/success/green?icon=github)](https://github.com/ithewei/libhv/actions/workflows/CI.yml?query=branch%3Amaster)
[![macOS](https://badgen.net/badge/macOS/success/green?icon=github)](https://github.com/ithewei/libhv/actions/workflows/CI.yml?query=branch%3Amaster)
[![Android](https://badgen.net/badge/Android/success/green?icon=github)](https://github.com/ithewei/libhv/actions/workflows/CI.yml?query=branch%3Amaster)
[![iOS](https://badgen.net/badge/iOS/success/green?icon=github)](https://github.com/ithewei/libhv/actions/workflows/CI.yml?query=branch%3Amaster)
[![benchmark](https://github.com/ithewei/libhv/workflows/benchmark/badge.svg?branch=master)](https://github.com/ithewei/libhv/actions/workflows/benchmark.yml?query=branch%3Amaster)
<br>
[![release](https://badgen.net/github/release/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/releases)
[![stars](https://badgen.net/github/stars/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/stargazers)
[![forks](https://badgen.net/github/forks/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/forks)
[![issues](https://badgen.net/github/issues/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/issues)
[![PRs](https://badgen.net/github/prs/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/pulls)
[![contributors](https://badgen.net/github/contributors/ithewei/libhv?icon=github)](https://github.com/ithewei/libhv/contributors)
[![license](https://badgen.net/github/license/ithewei/libhv?icon=github)](LICENSE)
<br>
[![gitee](https://badgen.net/badge/mirror/gitee/red)](https://gitee.com/libhv/libhv)
[![awesome-c](https://badgen.net/badge/icon/awesome-c/pink?icon=awesome&label&color)](https://github.com/oz123/awesome-c)
[![awesome-cpp](https://badgen.net/badge/icon/awesome-cpp/pink?icon=awesome&label&color)](https://github.com/fffaraz/awesome-cpp)

A cross-platform C/C++ network library for TCP, UDP, SSL/TLS, HTTP, WebSocket and MQTT client/server development, with event loop, simple APIs, and practical examples.

Like `libevent`, `libev`, and `libuv`, `libhv` provides non-blocking IO and timers. Compared with lower-level event libraries, `libhv` also includes higher-level protocol support and ready-to-run examples for common networking tasks.

## Why libhv

Choose `libhv` if you want:

- a cross-platform C/C++ network library with both low-level and high-level APIs
- a simpler API for common networking tasks
- built-in support for HTTP, WebSocket, MQTT, SSL/TLS, and event loop programming
- practical client/server examples instead of assembling multiple libraries first
- one library that can cover TCP, UDP, HTTP, WebSocket, and MQTT use cases

`libhv` is a good fit for developers who need:

- TCP/UDP client, server, and proxy development
- HTTP client/server, including HTTPS, HTTP/1.x, HTTP/2, and gRPC support
- WebSocket client/server support
- MQTT client support
- both C APIs and C++ wrappers in one project

## Feature overview

### Core networking
- Cross-platform: Linux, Windows, macOS, Android, iOS, BSD, Solaris
- High-performance EventLoop: IO, timer, idle, custom events, signals
- TCP/UDP client, server, proxy
- TCP heartbeat, reconnect, upstream, multi-thread-safe write and close
- Built-in unpacking modes: FixedLength, Delimiter, LengthField
- RUDP support via `WITH_KCP`

### Protocols
- SSL/TLS via `WITH_OPENSSL`, `WITH_GNUTLS`, or `WITH_MBEDTLS`
- HTTP client/server: HTTPS, HTTP/1.x, HTTP/2, gRPC
- HTTP static service, index service, forward/reverse proxy, sync/async handlers
- HTTP router, middleware, keep-alive, chunked, SSE
- WebSocket client/server
- MQTT client

### Tooling and ecosystem
- Makefile, CMake, Bazel, vcpkg, xmake
- Runnable examples under `examples/`, `evpp/`, and `examples/mqtt`
- Benchmark workflow on GitHub Actions
- C API entry via `hv.h`, C++ APIs via module headers such as `HttpServer.h`, `TcpServer.h`, `WebSocketServer.h`

## 30-second quick start

Build with Makefile:

```shell
./configure
make
```

Run the built-in HTTP server:

```shell
bin/httpd -d
bin/curl -v http://127.0.0.1:8080/ping
```

Or try the full walkthrough:

```shell
./getting_started.sh
```

For more build methods and options, see [BUILD.md](BUILD.md).

## Minimal example entry points

### HTTP server
See [examples/http_server_test.cpp](examples/http_server_test.cpp).

```c++
#include "HttpServer.h"
using namespace hv;

int main() {
    HttpService router;
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    HttpServer server(&router);
    server.setPort(8080);
    server.setThreadNum(4);
    server.run();
    return 0;
}
```

### HTTP client
See [examples/http_client_test.cpp](examples/http_client_test.cpp).

```c++
#include "requests.h"

int main() {
    auto resp = requests::get("http://www.example.com");
    if (resp) {
        printf("%s\n", resp->body.c_str());
    }
    return 0;
}
```

### TCP server
See [examples/tcp_echo_server.c](examples/tcp_echo_server.c) and [evpp/TcpServer_test.cpp](evpp/TcpServer_test.cpp).

### WebSocket
See [examples/websocket_server_test.cpp](examples/websocket_server_test.cpp) and [examples/websocket_client_test.cpp](examples/websocket_client_test.cpp).

### MQTT
See [examples/mqtt](examples/mqtt).

## Docs and examples

- Build and install: [BUILD.md](BUILD.md)
- API manual: [docs/API.md](docs/API.md)
- Example index: [examples/README.md](examples/README.md)
- HTTP server example: [examples/http_server_test.cpp](examples/http_server_test.cpp)
- HTTP client example: [examples/http_client_test.cpp](examples/http_client_test.cpp)
- WebSocket server example: [examples/websocket_server_test.cpp](examples/websocket_server_test.cpp)
- WebSocket client example: [examples/websocket_client_test.cpp](examples/websocket_client_test.cpp)
- TCP C++ examples: [evpp](evpp)
- MQTT examples: [examples/mqtt](examples/mqtt)

## Build and optional features

Supported build methods:

- Makefile
- CMake
- Bazel
- vcpkg
- xmake

Common optional features:

- `WITH_OPENSSL` for SSL/TLS
- `WITH_GNUTLS` for SSL/TLS
- `WITH_MBEDTLS` for SSL/TLS
- `WITH_NGHTTP2` for HTTP/2
- `WITH_KCP` for reliable UDP
- `WITH_MQTT` for MQTT
- `WITH_CURL` for curl-related support

Examples:

```shell
./configure --with-openssl --with-nghttp2 --with-kcp --with-mqtt
make
```

```shell
mkdir build && cd build
cmake .. -DWITH_OPENSSL=ON -DWITH_NGHTTP2=ON -DWITH_KCP=ON
cmake --build .
```

See [BUILD.md](BUILD.md) for platform-specific instructions, cross-compilation, and more options.

## Benchmark

`libhv` includes benchmark scripts and GitHub Actions benchmark runs. For detailed results and raw output, see:

- [benchmark workflow](https://github.com/ithewei/libhv/actions/workflows/benchmark.yml)
- [echo-servers](echo-servers)
- benchmark section in this repository's history and documentation

## Community and mirrors

- GitHub: <https://github.com/ithewei/libhv>
- Gitee mirror: <https://gitee.com/libhv/libhv>
- English / Chinese README: this page and [README-CN.md](README-CN.md)

For Chinese tutorials, community links, and user cases, see [README-CN.md](README-CN.md).
