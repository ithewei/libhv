UDP 客户端类

```c++

class UdpClient {

    // 返回所在的事件循环
    const EventLoopPtr& loop();

    // 创建套接字
    int createsocket(int remote_port, const char* remote_host = "127.0.0.1");

    // 绑定端口
    int bind(int local_port, const char* local_host = "0.0.0.0");

    // 关闭套接字
    void closesocket();

    // 开始运行
    void start(bool wait_threads_started = true);

    // 停止运行
    void stop(bool wait_threads_stopped = true);

    // 发送
    int sendto(const void* data, int size, struct sockaddr* peeraddr = NULL);
    int sendto(Buffer* buf, struct sockaddr* peeraddr = NULL);
    int sendto(const std::string& str, struct sockaddr* peeraddr = NULL);

    // 设置KCP
    void setKcp(kcp_setting_t* setting);

    // 消息回调
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;

    // 写完成回调
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;
};

```

测试代码见 [evpp/UdpClient_test.cpp](../../evpp/UdpClient_test.cpp)
