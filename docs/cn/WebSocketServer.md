WebSocket 服务端类

```c++

// WebSocketServer 继承自 HttpServer
class WebSocketServer : public HttpServer {

    // 注册WebSocket业务类
    void registerWebSocketService(WebSocketService* service);

};

// WebSocket业务类
struct WebSocketService {
    // 打开回调
    std::function<void(const WebSocketChannelPtr&, const HttpRequestPtr&)>  onopen;

    // 消息回调
    std::function<void(const WebSocketChannelPtr&, const std::string&)>     onmessage;

    // 关闭回调
    std::function<void(const WebSocketChannelPtr&)>                         onclose;

    // 心跳间隔
    int ping_interval;
};

```

测试代码见 [examples/websocket_server_test.cpp](../../examples/websocket_server_test.cpp)
