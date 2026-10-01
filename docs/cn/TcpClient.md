TCP 客户端类

```c++

class TcpClient {

    // 返回所在的事件循环
    const EventLoopPtr& loop();

    // 创建套接字
    int createsocket(int remote_port, const char* remote_host = "127.0.0.1");
    int createsocket(struct sockaddr* remote_addr);

    // 绑定端口
    int bind(int local_port, const char* local_host = "0.0.0.0");
    int bind(struct sockaddr* local_addr);

    // 关闭套接字
    void closesocket();

    // 开始运行
    void start(bool wait_threads_started = true);

    // 停止运行
    void stop(bool wait_threads_stopped = true);

    // 是否已连接
    bool isConnected();

    // 发送
    int send(const void* data, int size);
    int send(Buffer* buf);
    int send(const std::string& str);

    // 设置SSL/TLS加密通信
    int withTLS(hssl_ctx_opt_t* opt = NULL);

    // 设置连接超时
    void setConnectTimeout(int ms);

    // 设置重连
    void setReconnect(reconn_setting_t* setting);

    // 是否是重连
    bool isReconnect();

    // 设置拆包规则
    void setUnpack(unpack_setting_t* setting);

    // 连接状态回调
    std::function<void(const TSocketChannelPtr&)>           onConnection;

    // 消息回调
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;

    // 写完成回调
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

};

```

测试代码见 [evpp/TcpClient_test.cpp](../../evpp/TcpClient_test.cpp)

## TLS 证书验证

`withTLS()` 默认只启用 TLS，不验证对端证书。需要验证服务端身份时，
将 `verify_peer` 设为 `1`：

```cpp
hssl_ctx_opt_t ssl = {};
ssl.verify_peer = 1;
client.withTLS(&ssl);
```

未设置 `ca_file` 和 `ca_path` 时，Apple TLS、OpenSSL 和 GnuTLS 后端使用
系统信任根。设置了任一项时，仅信任指定的自定义 CA，不再使用系统信任根。
`ca_file` 可以包含多个 PEM `CERTIFICATE` 块，`ca_path` 指向包含 PEM 或
DER CA 证书的目录。通过域名连接时还会校验证书中的 DNS 主机名。

客户端双向 TLS 示例：

```cpp
hssl_ctx_opt_t ssl = {};
ssl.crt_file = "cert/client-chain.pem";
ssl.key_file = "cert/client.key";
ssl.ca_file = "cert/server-ca.pem";
ssl.verify_peer = 1;
client.withTLS(&ssl);
```

Apple TLS 后端的本地身份目前只支持未加密的 RSA 私钥：
`RSA PRIVATE KEY`（PKCS#1）或 RSA `PRIVATE KEY`（PKCS#8）。不支持 EC
私钥和加密私钥。加载本地身份要求 macOS 10.12 或 iOS 11.2 及以上；
不加载客户端身份时仍可在更早的系统版本上使用客户端 TLS。
