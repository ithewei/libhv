TCP 服务端类

```c++

class TcpServer {

    // 返回索引的事件循环
    EventLoopPtr loop(int idx = -1);

    // 创建套接字
    int createsocket(int port, const char* host = "0.0.0.0");

    // 关闭套接字
    void closesocket();

    // 设置最大连接数
    void setMaxConnectionNum(uint32_t num);

    // 设置负载均衡策略
    // 可选: LB_RoundRobin(轮询, 默认) / LB_Random(随机) /
    //       LB_LeastConnections(最少连接数) / LB_IpHash(按客户端IP哈希, 同一IP固定分配到同一worker)
    void setLoadBalance(load_balance_e lb);

    // 设置线程数
    void setThreadNum(int num);

    // 开始运行
    void start(bool wait_threads_started = true);

    // 停止运行
    void stop(bool wait_threads_stopped = true);

    // 设置SSL/TLS加密通信
    int withTLS(hssl_ctx_opt_t* opt = NULL);

    // 设置拆包规则
    void setUnpack(unpack_setting_t* setting);

    // 返回当前连接数
    size_t connectionNum();

    // 遍历连接
    int foreachChannel(std::function<void(const TSocketChannelPtr& channel)> fn);

    // 广播消息
    int broadcast(const void* data, int size);
    int broadcast(const std::string& str);

    // 连接到来/断开回调
    std::function<void(const TSocketChannelPtr&)>           onConnection;

    // 消息回调
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;

    // 写完成回调
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

};

```

测试代码见 [evpp/TcpServer_test.cpp](../../evpp/TcpServer_test.cpp)

## TLS 证书与双向认证

TLS 服务端必须提供证书链和对应私钥：

```cpp
hssl_ctx_opt_t ssl = {};
ssl.crt_file = "cert/server-chain.pem";
ssl.key_file = "cert/server.key";
server.withTLS(&ssl);
```

证书链文件使用 PEM 格式，第一张为服务端证书，后续依次为中间证书。

将 `verify_peer` 设为 `1` 会启用强制双向 TLS：客户端必须提供受信任的
证书，否则握手失败。

```cpp
hssl_ctx_opt_t ssl = {};
ssl.crt_file = "cert/server-chain.pem";
ssl.key_file = "cert/server.key";
ssl.ca_file = "cert/client-ca.pem";
ssl.verify_peer = 1;
server.withTLS(&ssl);
```

未设置 `ca_file` 和 `ca_path` 时使用系统信任根；设置了任一项时，仅信任
指定的自定义 CA。`ca_file` 可以包含多张 PEM 证书，`ca_path` 可以包含
PEM 或 DER CA 证书。

Apple TLS 后端的本地身份目前只支持未加密的 RSA 私钥：
`RSA PRIVATE KEY`（PKCS#1）或 RSA `PRIVATE KEY`（PKCS#8）。不支持 EC
私钥和加密私钥，且服务端身份要求 macOS 10.12 或 iOS 11.2 及以上。
