WebSocket 客户端类

```c++

class WebSocketClient {

    // 打开回调
    std::function<void()> onopen;
    // 关闭回调
    std::function<void()> onclose;
    // 在 onclose 中查询本次关闭状态
    int getCloseCode() const;
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

在 `onclose()` 中调用 `getCloseCode()` 可区分以下情况。每次新连接的 `onopen()` 中，初始值都是 `NoCloseFrame`。

| 返回值 | 含义 |
| --- | --- |
| 非负整数 | 对端 CLOSE 帧携带的 16 位状态码，例如 `1000` 或应用自定义的 `4000` |
| `WebSocketClient::CloseCode::NoCloseStatus` | 已收到对端 CLOSE 帧，负载中没有完整的状态码 |
| `WebSocketClient::CloseCode::LocalClose` | 应用主动关闭客户端，关闭前未收到对端 CLOSE 帧 |
| `WebSocketClient::CloseCode::NoCloseFrame` | 连接中断或连接尝试失败，未收到对端 CLOSE 帧 |

测试代码见 [examples/websocket_client_test.cpp](../../examples/websocket_client_test.cpp)
