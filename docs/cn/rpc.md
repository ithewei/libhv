# libhv RPC (hrpc)

hrpc 是 libhv 自带的轻量 RPC，两端均基于 libhv。传输走自定义 TCP，序列化用 protobuf，服务桩代码由 protoc 插件生成。默认不依赖 nghttp2，不追求与 gRPC 线上兼容。

分三层，自底向上：

1. **TLV 编解码层**：通用的 `type-length-value` 帧编解码，不依赖 IO，可被任意二进制协议复用。
2. **TLV 三件套**（`evpp/`）：`TLVChannel` / `TLVClient` / `TLVServer`，把 TLV 编解码接到 evpp 事件循环，按整帧投递。
3. **RPC 层**（`rpc/`）：`RpcClient` / `RpcServer` 继承 TLV 三件套，处理服务路由、请求应答关联、超时、心跳。

---

## 一、TLV 编解码层

TLV 是通用编解码单元，T 和 L 的字节宽度均可配置，让不同协议自由裁剪帧头。

```
+----------------+------------------+------------------+
| Type (T bytes) | Length (L bytes) | Value (Length)   |
+----------------+------------------+------------------+
```

### 配置

```cpp
typedef struct tlv_setting_s {
    unsigned char type_bytes;    // T 宽度: 0/1/2/4/8, 默认 4; 0 表示无 Type (纯 LV)
    unsigned char length_bytes;  // L 宽度: 1/2/4, 默认 4; 底层拆包按 32 位处理长度字段,
                                 // >4 会被 tlv_unpack_setting 钳制到 4
    bool          big_endian;    // 默认 true (网络字节序)
} tlv_setting_t;
```

由配置推算底层 `unpack_setting_t`（`UNPACK_BY_LENGTH_FIELD`）：

```
length_field_offset = type_bytes
length_field_bytes  = length_bytes
body_offset         = type_bytes + length_bytes
length_field_coding = big_endian ? ENCODE_BY_BIG_ENDIAN : ENCODE_BY_LITTLE_ENDIAN
```

### TLVMessage 类

Type 用定长字节数组承载（最大 8 字节），既能当整数用，也能按字节位切分子字段。类名用 `TLVMessage`（而非 `TLV`）以避免与全大写宏冲突；头文件为 `evpp/TLVMessage.h`。

```cpp
class TLVMessage {
public:
    // Type: 原始字节 / 按位 / 整数三种视图
    const unsigned char* type() const;
    void        setType(const void* data, int len);
    unsigned char typeAt(int i) const;
    void        setTypeAt(int i, unsigned char b);
    uint64_t    typeInt(const tlv_setting_t* setting) const;  // 按宽度/字节序解释为整数
    void        setTypeInt(uint64_t v, const tlv_setting_t* setting);

    // Length: 只读，随 setValue 自动维护
    uint64_t    length() const;

    // Value
    const char* value() const;
    void        setValue(const void* data, uint64_t len);

    // 编解码 (按 setting 决定 T/L 宽度与字节序)
    // pack:   写出 [T|L|V] 到 buf, 返回写入字节数, 不足返回 <0
    // unpack: 从 buf 解析 T/L/V (Value 拷贝到内部), 返回整帧长度, 不足返回 <0
    int packSize(const tlv_setting_t* setting) const;
    int pack(void* buf, int cap, const tlv_setting_t* setting) const;
    int unpack(const void* buf, int len, const tlv_setting_t* setting);
};
```

`length` 用 `uint64_t`，声明宽度 8 字节时理论上限 16EB，但实际拆包仍受底层 `package_max_length` 限制，不会因为声明大宽度就分配巨型 buffer。

---

## 二、TLV 三件套（evpp/）

复刻 libhv 已有的 WebSocket 三件套模式（`WebSocketChannel` / `WebSocketClient` / `WebSocketServer`）。

- `TLVChannel : SocketChannel` —— 提供 `sendTLV(type, data, len)`，内部用 TLV 编解码封帧。
- `TLVClient : TcpClientTmpl<TLVChannel>` —— 构造接受 `tlv_setting_t`（默认 T=4/L=4/大端），自动推算并 `setUnpack`；内部接管基类 `onMessage` 做整帧解析，对上层暴露高层回调。
- `TLVServer : TcpServerTmpl<TLVChannel>` —— 同上。

对上层暴露的高层回调（与 WebSocket 风格一致）：

```cpp
// 收到一整帧
std::function<void(const TLVChannelPtr& channel, const TLVMessage& msg)> onmessage;
```

三件套只负责“完整帧的收发”，不含任何 RPC 语义，可被 IM、游戏等其它 TLV 协议直接继承复用。

---

## 三、RPC 层（rpc/）

### 帧头（Type 的 8 字节切分）

hrpc 令 `type_bytes = 8`，把 Type 切成子字段（等价于早期 protorpc 的定长头）：

| 字节      | 含义                                            |
|-----------|-------------------------------------------------|
| type[0-3] | magic `"hrpc"`                                  |
| type[4]   | version = `1`                                   |
| type[5]   | 消息类型: REQUEST/RESPONSE/PING/PONG/CLOSE      |
| type[6-7] | reserved                                        |

- **REQUEST / RESPONSE**：Value 承载 protobuf 信封 `RpcMessage`。
- **PING / PONG**：心跳，Value 为空。
- **CLOSE**：优雅关闭，Value 为空。

`length_bytes = 4`（Value 即信封长度）。

### 信封 rpc.proto

```proto
syntax = "proto3";
package hv.rpc;

message RpcMessage {
    uint64 id      = 1;   // 请求/响应关联 id
    string method  = 2;   // "package.Service.Method"
    int32  status  = 3;   // 0=OK, 非0=错误码 (仅 response)
    string message = 4;   // 错误描述 (仅 response)
    bytes  payload = 5;   // 用户 request/response message 的序列化
}
```

一次 parse 信封即拿到路由信息（method）、关联 id、错误状态；`payload` 再由生成的桩代码按具体类型 parse。

### RpcServer

```cpp
class RpcServer : public TLVServer {
public:
    // 注册一个 service (由 codegen 生成的 RpcService 子类)
    void registerService(const std::shared_ptr<RpcService>& service);
};
```

`RpcService` 是 codegen 生成的服务基类，内部持有 `method -> handler` 表并向 `RpcServer` 注册。收到 REQUEST 帧后，`RpcServer` 解析信封、按 `method` 路由到对应 service，执行后把结果打包成 RESPONSE 帧回写。

> **线程约束（重要）**
> - `registerService()` 必须在 `start()` **之前**调用：method 表在 IO 线程无锁读取，start 后再注册是数据竞争。
> - service 方法**同步运行在连接所属的 IO 线程**上，同一连接的所有调用串行执行。慢 handler 会阻塞该 IO 线程上的其它连接。重活请交给自己的工作线程/线程池处理后再回复。

### RpcClient

```cpp
class RpcStatus {                 // RPC 调用结果 (区别于 evpp/Status.h 的生命周期状态)
public:
    int         code;             // 0=OK
    std::string message;
    bool ok() const { return code == 0; }
};

class RpcClient : public TLVClient {
public:
    // 同步 (禁止在所属 loop 线程内调用, 内部 future 等待)
    RpcStatus call(const std::string& method, const std::string& reqData,
                   std::string* respData, int timeout_ms = 10000);
    // 异步 (回调在 loop 线程触发)
    void callAsync(const std::string& method, const std::string& reqData,
                   std::function<void(const RpcStatus&, const std::string&)> cb,
                   int timeout_ms = 10000);
    void setPingInterval(int ms);   // 0 关闭心跳; 默认 3000ms
};
```

- 内部维护 `id -> 调用上下文` 表，收到 RESPONSE 按 id 匹配。
- 复用 `TcpClient` 的重连、EventLoop 所有权体系。
- **超时**：同步靠 future 等待 `timeout_ms`；异步挂一个 loop 定时器，到点回调 `HRPC_STATUS_TIMEOUT`。
- **断线**：连接关闭时，所有在途调用立即以 `HRPC_STATUS_NOT_CONNECTED` 失败（不会挂死、不泄漏上下文）。
- **心跳**：连接建立后定时发 PING（`setPingInterval`，默认 3000ms），收 PONG 重置计数；连续 3 次无 PONG 主动关闭连接（触发重连）。对端 PING 自动回 PONG。

---

## 四、代码生成（rpc/protoc-gen-hrpc）

标准 protobuf service 定义：

```proto
service Calc {
    rpc Add (CalcRequest) returns (CalcReply);
}
```

`protoc-gen-hrpc` 是 protoc 插件（依赖 `libprotoc`），protoc 把已解析的 AST 从 stdin 传入，插件遍历 `ServiceDescriptor` 套模板输出 `calc.hrpc.h`，无需自己写任何 IDL 解析。

生成产物：

**Server —— 用户继承实现纯虚方法**
```cpp
class CalcService : public hv::rpc::RpcService {
public:
    virtual hv::rpc::RpcStatus Add(const CalcRequest& req, CalcReply* reply) = 0;
    // 自动填充 method 表: "Calc.Add" -> 内部 trampoline (parse payload -> Add -> serialize)
};
```

**Client —— 生成类型安全的 stub**
```cpp
class CalcStub {
public:
    explicit CalcStub(hv::rpc::RpcClient* client);
    // 同步
    hv::rpc::RpcStatus Add(const CalcRequest& req, CalcReply* reply, int timeout_ms = 10000);
    // 异步
    void Add(const CalcRequest& req,
             std::function<void(const hv::rpc::RpcStatus&, const CalcReply&)> cb);
};
```

生成方式沿用 `examples/protorpc/proto/protoc.sh` 的一键脚本风格：
```bash
protoc --plugin=protoc-gen-hrpc=./protoc-gen-hrpc \
       --cpp_out=. --hrpc_out=. calc.proto
```

---

## 五、构建与使用

hrpc 依赖 protobuf，因此**单独编译成 `libhrpc`**，让 `libhv` 本身保持零 protobuf 依赖。三者关系：

```
用户代码 (含 protoc-gen-hrpc 生成的 xxx.hrpc.h)
   → libhrpc (RpcClient/RpcServer + rpc.pb 信封)   依赖 protobuf
   → libhv   (TLV 三件套 + evpp + event...)          零 protobuf
```

- **TLV 三件套**（`evpp/TLVMessage.h` + `TLV{Channel,Client,Server}.h`）不依赖 protobuf，随 evpp 一起编译安装（`include/hv/`），可被其它二进制协议独立复用。
- **libhrpc**（可选，默认关）：`RpcClient.cpp`/`RpcServer.cpp` + 信封 `rpc.pb.cc`，链接 libhv + protobuf。头文件安装到 **`include/hv/rpc/`**。
- **protoc-gen-hrpc** 插件安装到 `bin/`，用户用它生成 service stub。
- 现代 protobuf/abseil 头要求 C++17，故 libhrpc 以 C++17 编译；libhv/TLV 仍是 C++11。

### 编译 libhrpc

```bash
# Makefile (homebrew 环境指定 protobuf 前缀)
./configure --with-rpc
make libhv && make libhrpc PROTOBUF_PREFIX=/opt/homebrew
sudo make install WITH_RPC=yes   # 安装 libhrpc + include/hv/rpc + bin/protoc-gen-hrpc

# CMake
cmake .. -DWITH_RPC=ON -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build . --target hrpc
```

### 用户使用（基于发布产物）

```bash
# 1. 用安装的插件生成 stub
protoc --plugin=protoc-gen-hrpc=$(which protoc-gen-hrpc) \
       --cpp_out=. --hrpc_out=. myservice.proto
# 2. 编译链接: -lhrpc -lhv -lprotobuf
g++ -std=c++17 myapp.cpp myservice.pb.cc \
    -I<prefix>/include/hv -I<prefix>/include/hv/rpc \
    -lhrpc -lhv -lprotobuf
```

代码里 `#include <hv/rpc/RpcClient.h>` + `#include "myservice.hrpc.h"` 即可。

---

## 六、范围

**第一版包含**：unary RPC、同步 + 异步调用、service/method 路由、错误传递、超时、心跳（PING/PONG）、优雅关闭（CLOSE）、断线重连。

**暂不包含（后续）**：streaming（server/client/bidi stream）、TLS（后续复用 libhv ssl 层）、Lua 绑定。
