# hdns: 异步DNS解析

`hdns` 是 `libhv` 内置的**异步 DNS 解析器**，完全运行在事件循环（hloop）之内。

## 为什么需要它

`libhv` 是一个事件循环库，但此前域名解析走的是**阻塞的** `getaddrinfo()`（`base/hsocket.c` 的 `ResolveAddr`）。在事件循环里同步解析 DNS 会**卡住整个 loop**——期间所有其它连接的读写、定时器都会停摆，网络抖动或 DNS 不可达时甚至会阻塞数秒。

`hdns` 参考 `libevent` 的 `evdns` 思路，**自己实现了一套原生的、基于非阻塞 UDP 的 DNS 解析器**：构造 DNS 报文、通过 hloop 的非阻塞 UDP 收发、解析响应、处理超时/重试，全程不阻塞事件循环，也**不引入任何第三方依赖**（不同于 libuv 的线程池方案，也不同于接入 c-ares）。

> 注意：`hdns` 与 `protocol/dns.*` 相互独立。后者是早期的同步 demo（仅供学习），`hdns` 自带一套完整、独立的 DNS 报文实现。

`hdns` 属于 `event` 核心模块，**默认编入**，无需额外编译开关。对外头文件：`event/hdns.h`。

## 特性

- 异步 A（IPv4）/ AAAA（IPv6）查询，纯事件驱动。
- 自动读取系统 nameserver：
  - Unix/Linux/macOS：解析 `/etc/resolv.conf`；
  - Windows：`GetAdaptersAddresses()`（链接 `iphlpapi`）；
  - 任意平台若拿不到 nameserver，统一回退到 `8.8.8.8`。
- 加载 `/etc/hosts`（Windows 为 `%SystemRoot%\System32\drivers\etc\hosts`），查询前先查表，`localhost` 等本地映射行为与系统一致。
- 数字 IP 快速路径（字面量 IPv4/IPv6 不发起查询）。
- 每次查询支持超时、重试、多 nameserver 轮转。
- DNS 名字压缩解析。
- **尊重 TTL 的进程内缓存**（正向缓存 + 负向缓存）。
- 可取消的查询句柄。
- 结果直接以 `sockaddr_u` 返回，拿到即可用于 connect。

> 第一版**暂不包含**：search domains / ndots、TCP fallback（TC 位截断重查）、nameserver 健康探测、SRV/TXT/MX 等裸记录查询。这些留作后续增强。

## 接口

```c
#include "hdns.h"

// 查询地址族
typedef enum {
    HDNS_QUERY_A    = 0x01,     // 仅 IPv4
    HDNS_QUERY_AAAA = 0x02,     // 仅 IPv6
    HDNS_QUERY_BOTH = 0x03,     // A + AAAA（默认）
} hdns_family_e;

// 查询选项（可选；传 NULL 使用默认值）
typedef struct hdns_setting_s {
    hdns_family_e   family;     // 默认 HDNS_QUERY_BOTH
    int             timeout_ms; // 每次尝试的超时，默认 5000ms
    int             retries;    // 重试次数，默认 2（总尝试次数 = retries + 1）
    int             use_cache;  // 是否使用缓存，默认 1
    const char*     nameserver; // 可选，覆盖 nameserver，形如 "8.8.8.8" 或 "8.8.8.8:53"；NULL 表示自动
} hdns_setting_t;

// 解析结果
typedef struct hdns_result_s {
    int         status;                     // 0 成功；<0 为错误码（HDNS_STATUS_*）
    char        host[HDNS_NAME_MAXLEN];     // 查询的域名
    int         naddrs;                     // 解析出的地址数量
    sockaddr_u  addrs[HDNS_MAX_ADDRS];      // A/AAAA 合并结果（IPv4 在前），端口为 0
} hdns_result_t;

// 查询句柄：hdns_t 是一个事件对象（继承自 hevent_t，与 htimer_t / hio_t 同源），
// 因此可携带 event_id、复用 hevent_set_id/hevent_id。
typedef struct hdns_s hdns_t;

// 解析完成回调，在 loop 线程被调用。
// query: 本次查询句柄；回调返回后立即被释放，不要保留。可在此读取其 event_id
//        （hevent_id）与调用方的 id 映射关联。result 仅在回调期间有效。
typedef void (*hdns_cb)(hdns_t* query, const hdns_result_t* result, void* userdata);

// 发起异步解析，绑定到 loop。返回查询句柄，失败返回 NULL。
hdns_t* hdns_resolve(hloop_t* loop, const char* host,
                     hdns_cb cb, void* userdata);

// 带选项版本
hdns_t* hdns_resolve_ex(hloop_t* loop, const char* host,
                        const hdns_setting_t* opt,
                        hdns_cb cb, void* userdata);

// 取消进行中的查询并立即释放；返回后回调不会再被调用。
// 只能在 loop 线程调用，且不得在该查询自己的回调内调用。
void hdns_cancel(hdns_t* query);

// 清空该 loop 的 DNS 缓存（如网络切换时）。只能在 loop 线程调用。
void hdns_clear_cache(hloop_t* loop);
```

> C 层句柄的生命周期契约与 `htimer_t` 一致：句柄在查询完成（回调触发）或被 `hdns_cancel` 后失效，之后不得再使用。**如果需要免疫 use-after-free 的句柄，使用 C++ 层 `EventLoop::resolveDns()` / `cancelDns()`**——它维护 `DnsID → hdns_t` 映射（对标 `TimerID`），失效的 `DnsID` 在 `cancelDns` 里查不到即安全 no-op。

### 状态码

```c
#define HDNS_STATUS_OK              0    // 成功
#define HDNS_STATUS_TIMEOUT       (-1)   // 所有尝试均超时
#define HDNS_STATUS_NXDOMAIN      (-2)   // 无此域名 / 无地址记录
#define HDNS_STATUS_SERVFAIL      (-3)   // 服务器失败 / 响应异常
#define HDNS_STATUS_BADNAME       (-4)   // 非法域名
#define HDNS_STATUS_NONAMESERVER  (-5)   // 无可用 nameserver
#define HDNS_STATUS_NOMEM         (-6)   // 内存不足
#define HDNS_STATUS_CANCELLED     (-7)   // 已取消（不会回调）
#define HDNS_STATUS_ERROR         (-8)   // 其它错误
```

## 回调时序保证

`hdns_resolve` **绝不会在调用内部同步触发回调**。即使是数字 IP、`/etc/hosts` 命中、缓存命中这类“立即有结果”的情况，完成也会被投递到**下一次 loop 迭代**再回调。因此调用方总能先拿到有效句柄，随后可以确定性地保存或取消它，不会出现回调重入或 use-after-free。

## 示例

```c
#include "hloop.h"
#include "hdns.h"
#include "hsocket.h"

static int pending = 0;

static void on_resolved(hdns_t* query, const hdns_result_t* result, void* userdata) {
    hloop_t* loop = (hloop_t*)userdata;
    (void)query;
    if (result->status == HDNS_STATUS_OK) {
        printf("%s =>\n", result->host);
        for (int i = 0; i < result->naddrs; ++i) {
            char ip[SOCKADDR_STRLEN] = {0};
            sockaddr_ip((sockaddr_u*)&result->addrs[i], ip, sizeof(ip));
            printf("    %s\n", ip);
        }
    } else {
        printf("%s => 解析失败, status=%d\n", result->host, result->status);
    }
    if (--pending == 0) hloop_stop(loop);
}

int main() {
    hloop_t* loop = hloop_new(0);
    const char* hosts[] = { "localhost", "www.example.com", "github.com" };
    pending = 3;
    for (int i = 0; i < 3; ++i) {
        hdns_resolve(loop, hosts[i], on_resolved, loop);
    }
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
```

完整示例见 `examples/host.c`，性能对比见 `unittest/hdns_benchmark.c`。

## C++ 层:EventLoop::resolveDns

C++ 客户端不直接用 C 层裸指针句柄,而是通过 `EventLoop` 提供的 id 化接口(对标 `setTimer`/`killTimer` 的 `TimerID`):

```cpp
// 返回免疫 use-after-free 的 DnsID(0 = 失败);cb 在 loop 线程回调
DnsID resolveDns(const char* host, DnsCallback cb, const hdns_setting_t* opt = NULL);
void  cancelDns(DnsID id);   // 线程安全;失效 id 自动 no-op
// DnsCallback: void(int status, int naddrs, const sockaddr_u* addrs)
```

`EventLoop` 内部维护 `DnsID → hdns_t*` 映射:查询完成时在完成回调里擦除映射项,`cancelDns` 按 id 查表(查不到即安全 no-op)。因此上层只持有一个 `DnsID` 整数,**即使底层 `hdns_t` 已被释放,拿旧 id 去 cancel 也不会 UAF**——与 `TimerID` 完全同构。

## 与 connect 路径的集成

`hdns` 已接入 C++ 客户端的 connect 路径。**异步解析逻辑统一沉淀在基类 `TcpClientEventLoopTmpl`（`evpp/TcpClient.h`）里**,并通过上面的 `EventLoop::resolveDns` 使用 `DnsID` 句柄,因此所有继承自 `TcpClientTmpl` 的客户端（`TcpClient`、`WebSocketClient`,以及未来任何 `XXXClient`）都**自动获得**异步 DNS,无需各自编写胶水代码。

### TcpClientTmpl 派生类（TcpClient / WebSocketClient / ...）

- 目标是**数字 IP**（或 Unix Domain Socket）时，`createsocket` 走原有同步快速路径立即建 socket，行为不变；
- 目标是**域名**时，`createsocket` 只记录 host/port 并**延迟解析**（不阻塞）；`startConnect()`（首次连接与每次重连都会调用）先用 `EventLoop::resolveDns` 异步解析，拿到地址后再在 `startConnectWithAddr()` 建 socket、connect。整个过程**不阻塞事件循环**。旧实现在 loop 线程里调用阻塞的 `getaddrinfo`，会卡住整个 loop。
- 每次连接/重连都会重新解析，自动应对 DNS 变化；
- 对象只持有 `DnsID`；析构或主动 `closesocket()` 时 `cancelDns` 即可，**无悬垂指针风险**（销毁客户端时查询仍在途也安全,由 `unittest/tcpclient_dns_test` 覆盖）。
- 解析失败(含首次连接就失败、此时还没有 channel)时,会用一个 NULL-io 的 channel(`isConnected()` 为 false、各方法对空 io 已做防护)走 `onConnection(断开)` 通知,**保证用户总能收到失败回调**(而不是静默丢失);若配置了重连,则继续按重连策略重试。由 `unittest/tcpclient_dns_test` 覆盖。
- 用户可在 `onConnection` 里用 `channel->error()` 区分失败原因:DNS 解析失败返回 `ERR_DNS_RESOLVE`(见 `herr.h`),连接失败则返回底层 IO 错误(如 `ETIMEDOUT`、连接被拒等)。`Channel` 新增了 `setError()`/`error()`——`error()` 优先返回上层显式设置的错误码,否则回退到 `hio_error(io)`,且对空 io 安全。

> 新增客户端零成本:只要继承 `TcpClientTmpl` 并用 `createsocket(port, host)` + `start()`，就自动拥有异步 DNS，不需要实现任何 `onDnsResolved` 之类的回调。

### AsyncHttpClient

`AsyncHttpClient` 不继承 `TcpClientTmpl`（自带连接池等逻辑），单独接入,但同样使用 `EventLoop::resolveDns`:

- 请求 URL 里是**数字 IP**（或 Unix Domain Socket）时，走原有同步快速路径；
- 请求 URL 里是**域名**时，`doTask` 会先用 `EventLoop::resolveDns` 异步解析，回调里再建立连接、发送请求——同样**不阻塞 loop**。旧实现直接在 loop 线程里对域名做阻塞 `getaddrinfo`。
- 查询生命周期由 `EventLoop` 的 `DnsID` 映射管理,随 loop 拆除统一回收,无需客户端手工跟踪。

> 说明：同步的 `HttpClient` / `requests` 请求路径运行在调用方线程（并非在 loop 内），阻塞解析可接受，故该路径维持不变。

## 性能说明

`unittest/hdns_benchmark.c` 对比“顺序阻塞 `getaddrinfo`” 与 “单 loop 并发 `hdns`” 解析同一批域名：由于异步解析把所有查询一次性发出、由事件循环并发多路复用，总耗时约等于**一次最慢的解析**，而不是所有解析之和；更关键的是解析期间**事件循环始终不被阻塞**，其它 IO / 定时器可以继续运行。

> 注意：单次“命中缓存”的孤立解析，系统 `getaddrinfo`（下游有 `systemd-resolved` 等本地缓存）可能更快；`hdns` 通过尊重 TTL 的进程内缓存来弥补——命中缓存时是进程内查表（微秒级），且同样不阻塞 loop。
