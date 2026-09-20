SOCKS5 代理客户端

在事件循环(io)层内置的 SOCKS5 客户端代理支持(RFC 1928 + RFC 1929 用户名/密码认证)。

设置后，`hio_connect()` 会先连接到 SOCKS5 代理并完成握手(向代理发起 CONNECT 到目标地址，目标以域名形式发送，由代理解析），握手成功后连接对上层透明；若开启了 SSL，则在隧道之上再与目标做 TLS 握手。

由于挂在 `hio_connect` 上，所有基于它的客户端(`TcpClient`、`HttpClient` 等)都能直接使用。

> 说明：
> - 只做客户端代理(通过代理连出去)，服务端见 [examples/socks5_proxy_server.c](../../examples/socks5_proxy_server.c)。
> - 支持无认证与用户名/密码认证(不支持 GSSAPI)。
> - 目标为域名时以 ATYP=domain 发给代理解析(客户端本地不做 DNS)；为 IP 字面量时按 ATYP=ipv4/ipv6 发送。
> - `host` 建议直接填代理的 IP。若填域名，`hio_connect()` 会在事件循环线程内同步解析代理地址(getaddrinfo)，首连及每次重连都可能短暂阻塞该 loop。

## 配置结构 socks5_setting_t

```c
typedef struct socks5_setting_s {
    char host[256];     // 代理主机
    int  port;          // 代理端口
    char username[256]; // 空 => 无认证
    char password[256];
} socks5_setting_t;
```

> C 用户使用前请先清零：`socks5_setting_t s5; memset(&s5, 0, sizeof(s5));`（或 `= {0}`），
> 否则 username/password 为未初始化值会导致认证方式误判。C++ 有默认构造，无需手动清零。

## C 接口

```c
// 设置 SOCKS5 代理(setting 会被拷贝)；在 hio_connect() 之前调用。
int hio_set_socks5(hio_t* io, socks5_setting_t* setting);
```

## C++ 接口

```c++
namespace hv {

// SocketChannel
int SocketChannel::setSocks5Proxy(socks5_setting_t* setting);

// TcpClient
void TcpClient::setSocks5Proxy(socks5_setting_t* setting);

}
```

## 示例

### C

C 层没有异步 DNS，目标域名交给代理解析，所以**不要**用 `hio_create_socket(loop, target_host, ...)` 去本地解析目标(仅代理可达的域名会在这里失败)。正确做法是：用一个占位 host + **真实的目标端口**建 socket(host 只决定 socket 的地址族，`hio_connect()` 会按代理地址族重建 fd)，再用 `hio_set_hostname()` 把真实目标交给握手：

```c
#include "hloop.h"
#include "hbase.h"

// 注意：端口用真实目标端口；host 是占位符(只定地址族)，真实目标走 hio_set_hostname。
hio_t* io = hio_create_socket(loop, "127.0.0.1", target_port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
hio_set_hostname(io, target_host);   // 域名 => ATYP=domain(代理解析)；IP => ATYP=ipv4/ipv6

socks5_setting_t socks5;
memset(&socks5, 0, sizeof(socks5));
hv_strncpy(socks5.host, "127.0.0.1", sizeof(socks5.host));
socks5.port = 1080;
// 如需认证: hv_strncpy(socks5.username, "user", ...); hv_strncpy(socks5.password, "pass", ...);
hio_set_socks5(io, &socks5);

hio_setcb_connect(io, on_connect);
hio_setcb_close(io, on_close);
hio_connect(io);
```

完整示例见 [examples/socks5_client_test.c](../../examples/socks5_client_test.c)。

### C++

C++ 用 `TcpClient`，DNS 由其内部异步处理(配了代理时会跳过本地 DNS)，`createsocket(port, host)` 直接传真实目标即可：

```c++
#include "TcpClient.h"
using namespace hv;

int main() {
    TcpClient cli;
    cli.createsocket(1234, "target.example.com");   // 目标(可为域名，由代理解析)

    socks5_setting_t socks5;
    hv_strncpy(socks5.host, "127.0.0.1", sizeof(socks5.host));
    socks5.port = 1080;
    // 如需认证: hv_strncpy(socks5.username, "user", ...); hv_strncpy(socks5.password, "pass", ...);
    cli.setSocks5Proxy(&socks5);

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
