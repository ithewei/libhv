[English](README.md) | 中文

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

`libhv` 是一个跨平台的 C/C++ 网络库，支持 TCP、UDP、SSL/TLS、HTTP、WebSocket、MQTT 的客户端与服务端开发，提供 event loop、简洁 API 和可直接运行的示例。

它和 `libevent`、`libev`、`libuv` 一样提供非阻塞 IO 与定时器能力，但在常见网络开发场景里，`libhv` 还提供了更高层的协议支持和更完整的示例入口，适合希望快速落地网络程序的开发者。

## 为什么选择 libhv

如果你希望获得这些能力，`libhv` 会比较合适：

- 一个同时提供 C API 和 C++ API 的跨平台网络库
- 比底层事件库更接近业务开发的 API
- 内置 HTTP、WebSocket、MQTT、SSL/TLS、EventLoop 等常用能力
- 不想先拼装多套库，而是直接从示例开始搭 client/server
- 用一套库覆盖 TCP、UDP、HTTP、WebSocket、MQTT 等常见场景

`libhv` 特别适合这些使用需求：

- TCP/UDP 客户端、服务端、代理开发
- HTTP 客户端/服务端，包括 HTTPS、HTTP/1.x、HTTP/2、gRPC
- WebSocket 客户端/服务端
- MQTT 客户端
- 既需要 C 接口，也需要 C++ 封装

## 功能概览

### 网络基础能力
- 跨平台：Linux、Windows、macOS、Android、iOS、BSD、Solaris
- 高性能 EventLoop：IO、timer、idle、自定义事件、signal
- TCP/UDP 客户端、服务端、代理
- TCP 支持心跳、重连、转发、多线程安全 write/close
- 内置常见拆包模式：固定包长、分隔符、头部长度字段
- 通过 `WITH_KCP` 支持可靠 UDP

### 协议能力
- 通过 `WITH_OPENSSL`、`WITH_GNUTLS`、`WITH_MBEDTLS` 支持 SSL/TLS
- HTTP 客户端/服务端：HTTPS、HTTP/1.x、HTTP/2、gRPC
- HTTP 静态文件服务、目录服务、正向/反向代理、同步/异步 handler
- HTTP 路由、中间件、keep-alive、chunked、SSE
- WebSocket 客户端/服务端
- MQTT 客户端

### 构建与生态
- 支持 Makefile、CMake、Bazel、vcpkg、xmake
- 仓库内提供可直接运行的 `examples/`、`evpp/`、`examples/mqtt`
- GitHub Actions 中提供 benchmark workflow
- C 接口可从 `hv.h` 进入，C++ 接口可从 `HttpServer.h`、`TcpServer.h`、`WebSocketServer.h` 等模块头文件进入

## 30 秒快速体验

使用 Makefile 编译：

```shell
./configure
make
```

启动内置 HTTP 服务：

```shell
bin/httpd -d
bin/curl -v http://127.0.0.1:8080/ping
```

也可以直接运行完整体验脚本：

```shell
./getting_started.sh
```

更多构建方式和选项见 [BUILD.md](BUILD.md)。

## 最小示例入口

### HTTP 服务端
见 [examples/http_server_test.cpp](examples/http_server_test.cpp)。

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

### HTTP 客户端
见 [examples/http_client_test.cpp](examples/http_client_test.cpp)。

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

### TCP 服务端
见 [examples/tcp_echo_server.c](examples/tcp_echo_server.c) 和 [evpp/TcpServer_test.cpp](evpp/TcpServer_test.cpp)。

### WebSocket
见 [examples/websocket_server_test.cpp](examples/websocket_server_test.cpp) 和 [examples/websocket_client_test.cpp](examples/websocket_client_test.cpp)。

### MQTT
见 [examples/mqtt](examples/mqtt)。

## 文档与示例入口

- 构建与安装： [BUILD.md](BUILD.md)
- API 手册： [docs/API.md](docs/API.md)
- 示例索引： [examples/README.md](examples/README.md)
- HTTP 服务端示例： [examples/http_server_test.cpp](examples/http_server_test.cpp)
- HTTP 客户端示例： [examples/http_client_test.cpp](examples/http_client_test.cpp)
- WebSocket 服务端示例： [examples/websocket_server_test.cpp](examples/websocket_server_test.cpp)
- WebSocket 客户端示例： [examples/websocket_client_test.cpp](examples/websocket_client_test.cpp)
- TCP C++ 示例： [evpp](evpp)
- MQTT 示例： [examples/mqtt](examples/mqtt)

## 构建方式与可选特性

支持的构建方式：

- Makefile
- CMake
- Bazel
- vcpkg
- xmake

常见可选特性：

- `WITH_OPENSSL`：SSL/TLS
- `WITH_GNUTLS`：SSL/TLS
- `WITH_MBEDTLS`：SSL/TLS
- `WITH_NGHTTP2`：HTTP/2
- `WITH_KCP`：可靠 UDP
- `WITH_MQTT`：MQTT
- `WITH_CURL`：curl 相关支持

示例：

```shell
./configure --with-openssl --with-nghttp2 --with-kcp --with-mqtt
make
```

```shell
mkdir build && cd build
cmake .. -DWITH_OPENSSL=ON -DWITH_NGHTTP2=ON -DWITH_KCP=ON
cmake --build .
```

更多平台说明、交叉编译和其他选项见 [BUILD.md](BUILD.md)。

## 性能测试

`libhv` 仓库中提供 benchmark 脚本和 GitHub Actions benchmark 结果。详细数据与原始输出可见：

- [benchmark workflow](https://github.com/ithewei/libhv/actions/workflows/benchmark.yml)
- [echo-servers](echo-servers)

## 中文资料与社区

- **libhv QQ 群**：`739352073`
- **libhv 源码剖析**：<https://hewei.blog.csdn.net/article/details/123295998>
- **libhv 接口手册**：<https://hewei.blog.csdn.net/article/details/103976875>
- **libhv 教程目录**：<https://hewei.blog.csdn.net/article/details/113733758>
- [libhv 教程 01 - 介绍与体验](https://hewei.blog.csdn.net/article/details/113702536)
- [libhv 教程 02 - 编译与安装](https://hewei.blog.csdn.net/article/details/113704737)
- [libhv 教程 03 - 链库与使用](https://hewei.blog.csdn.net/article/details/113706378)
- [libhv 教程 04 - 编写一个完整的命令行程序](https://hewei.blog.csdn.net/article/details/113719503)
- [libhv 教程 05 - 事件循环以及定时器的简单使用](https://hewei.blog.csdn.net/article/details/113724474)
- [libhv 教程 06 - 创建一个简单的 TCP 服务端](https://hewei.blog.csdn.net/article/details/113737580)
- [libhv 教程 07 - 创建一个简单的 TCP 客户端](https://hewei.blog.csdn.net/article/details/113738900)
- [libhv 教程 08 - 创建一个简单的 UDP 服务端](https://hewei.blog.csdn.net/article/details/113871498)
- [libhv 教程 09 - 创建一个简单的 UDP 客户端](https://hewei.blog.csdn.net/article/details/113871724)
- [libhv 教程 10 - 创建一个简单的 HTTP 服务端](https://hewei.blog.csdn.net/article/details/113982999)
- [libhv 教程 11 - 创建一个简单的 HTTP 客户端](https://hewei.blog.csdn.net/article/details/113984302)
- [libhv 教程 12 - 创建一个简单的 WebSocket 服务端](https://hewei.blog.csdn.net/article/details/113985321)
- [libhv 教程 13 - 创建一个简单的 WebSocket 客户端](https://hewei.blog.csdn.net/article/details/113985895)
- [libhv 教程 14 - 200 行实现一个纯 C 版 jsonrpc 框架](https://hewei.blog.csdn.net/article/details/119920540)
- [libhv 教程 15 - 200 行实现一个 C++ 版 protorpc 框架](https://hewei.blog.csdn.net/article/details/119966701)
- [libhv 教程 16 - 多线程/多进程服务端编程](https://hewei.blog.csdn.net/article/details/120366024)
- [libhv 教程 17 - Qt 中使用 libhv](https://hewei.blog.csdn.net/article/details/120699890)
- [libhv 教程 18 - 动手写一个 tinyhttpd](https://hewei.blog.csdn.net/article/details/121706604)
- [libhv 教程 19 - MQTT 的实现与使用](https://hewei.blog.csdn.net/article/details/122753665)

## 用户案例

如果您在使用 `libhv`，欢迎通过 PR 将信息提交到这个列表，让更多用户了解 `libhv` 的实际使用场景。

| 用户 (公司名/项目名/个人联系方式) | 案例 (项目简介/业务场景) |
| :--- | :--- |
| [阅面科技](https://www.readsense.cn) | [猎户AIoT平台](https://orionweb.readsense.cn) 设备管理、人脸检测 HTTP 服务、人脸搜索 HTTP 服务 |
| [socks5-libhv](https://gitee.com/billykang/socks5-libhv) | socks5 代理 |
| [hvloop](https://github.com/xiispace/hvloop) | 类似 [uvloop](https://github.com/MagicStack/uvloop) 的 Python 异步 IO 事件循环 |
| [tsproxyd-android](https://github.com/Haiwen-GitHub/tsproxyd-android) | 一个基于 libhv 实现的 Android 端 web 代理服务 |
| [玄舟智维](https://zjzwxw.com) | C100K 设备连接网关服务 |
