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
