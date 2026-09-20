SOCKS5 代理客户端

在事件循环(io)层内置的客户端代理支持(目前实现 SOCKS5，RFC 1928 + RFC 1929 用户名/密码认证)。

设计上 socket 直接创建/连接到**代理**地址，`hio_connect()` 完成 TCP 连接后先跑代理握手(向代理发起 CONNECT 到目标)，握手成功后连接对上层透明；若开启了 SSL，则在隧道之上再与目标做 TLS 握手。

由于挂在 `hio_connect` 上，所有基于它的客户端(`TcpClient`、`HttpClient` 等)都能直接使用。

> 说明：
> - 只做客户端代理(通过代理连出去)，服务端见 [examples/socks5_proxy_server.c](../../examples/socks5_proxy_server.c)。
> - 支持无认证与用户名/密码认证(不支持 GSSAPI)。
> - 目标为域名时以 ATYP=domain 发给代理解析(客户端本地不做 DNS)；为 IP 字面量时按 ATYP=ipv4/ipv6 发送。
> - 目前仅实现 `PROXY_PROTOCOL_SOCKS5`。

## 配置结构 proxy_setting_t

```c
typedef enum {
    PROXY_PROTOCOL_NONE   = 0,
    PROXY_PROTOCOL_SOCKS5 = 1,
} proxy_protocol_e;

typedef struct proxy_setting_s {
    int  protocol;              // proxy_protocol_e，目前仅 SOCKS5
    char proxy_host[256];       // 代理主机(socket 连接到它)
    int  proxy_port;            // 代理端口
    char target_host[256];      // 最终目标(代理去 CONNECT)
    int  target_port;
    char username[256];         // 空 => 无认证
    char password[256];
} proxy_setting_t;
```

> C 用户使用前请先清零：`proxy_setting_t s; memset(&s, 0, sizeof(s));`（或 `= {0}`），
> 否则 username/password 为未初始化值会导致认证方式误判。C++ 有默认构造，无需手动清零。

## C 接口

```c
// 设置代理(setting 会被拷贝)；在 hio_connect() 之前调用。
// 注意：io 必须创建到代理地址，即 hio_create_socket(loop, proxy_host, proxy_port, ...)。
int hio_set_proxy(hio_t* io, proxy_setting_t* setting);
```

## C++ 接口

```c++
namespace hv {

// SocketChannel
int SocketChannel::setProxy(proxy_setting_t* setting);

// TcpClient
void TcpClient::setProxy(proxy_setting_t* setting);

}
```

## 示例

### C

C 层 socket 直接建到**代理**，目标 host/port 通过 `proxy_setting_t` 传入，由代理去 CONNECT/解析：

```c
#include "hloop.h"
#include "hbase.h"

// socket 建到代理地址
hio_t* io = hio_create_socket(loop, proxy_host, proxy_port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);

proxy_setting_t proxy;
memset(&proxy, 0, sizeof(proxy));
proxy.protocol = PROXY_PROTOCOL_SOCKS5;
hv_strncpy(proxy.target_host, target_host, sizeof(proxy.target_host)); // 域名 => ATYP=domain(代理解析)；IP => ATYP=ipv4/ipv6
proxy.target_port = target_port;
// 如需认证: hv_strncpy(proxy.username, "user", ...); hv_strncpy(proxy.password, "pass", ...);
hio_set_proxy(io, &proxy);

hio_setcb_connect(io, on_connect);
hio_setcb_close(io, on_close);
hio_connect(io);
```

完整示例见 [examples/socks5_client_test.c](../../examples/socks5_client_test.c)。

### C++

C++ 用 `TcpClient`：`createsocket(port, host)` 传真实目标(内部会作为 `target_*` 交给代理)，代理地址填在 `proxy_setting_t.proxy_host/proxy_port`。代理若是域名，由 `TcpClient` 内部异步解析(不阻塞 loop)：

```c++
#include "TcpClient.h"
using namespace hv;

int main() {
    TcpClient cli;
    cli.createsocket(1234, "target.example.com");   // 目标(可为域名，由代理解析)

    proxy_setting_t proxy;
    hv_strncpy(proxy.proxy_host, "127.0.0.1", sizeof(proxy.proxy_host));
    proxy.proxy_port = 1080;
    // 如需认证: hv_strncpy(proxy.username, "user", ...); hv_strncpy(proxy.password, "pass", ...);
    cli.setProxy(&proxy);

    cli.onConnection = [](const SocketChannelPtr& channel) {
        if (channel->isConnected()) channel->write("hello via socks5");
    };
    cli.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("recv: %.*s\n", (int)buf->size(), (char*)buf->data());
    };
    cli.start();
    while (1) hv_sleep(1);
    return 0;
}
```

可用 libhv 自带的 SOCKS5 代理服务端做端到端测试：

```sh
bin/tcp_echo_server 1234
bin/socks5_proxy_server 1080
bin/socks5_client_test 127.0.0.1 1080 127.0.0.1 1234
```
