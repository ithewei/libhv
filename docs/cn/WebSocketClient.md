WebSocket 客户端类

```c++

class WebSocketClient {

    // 打开回调
    std::function<void()> onopen;
    // 关闭回调
    std::function<void()> onclose;
    // 消息回调
    std::function<void(const std::string& msg)> onmessage;

    // 打开
    int open(const char* url, const http_headers& headers = DefaultHeaders);

    // 关闭
    int close();

    // 发送
    int send(const std::string& msg);
    int send(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY);

    // 设置心跳间隔
    void setPingInterval(int ms);

    // 设置WebSocket握手阶段的HTTP请求
    void setHttpRequest(const HttpRequestPtr& req);

    // 获取WebSocket握手阶段的HTTP响应
    const HttpResponsePtr& getHttpResponse();

};

```

测试代码见 [examples/websocket_client_test.cpp](../../examples/websocket_client_test.cpp)
