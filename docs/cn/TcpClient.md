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
