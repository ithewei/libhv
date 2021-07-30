#ifndef HV_WEBSOCKET_SERVER_H_
#define HV_WEBSOCKET_SERVER_H_

/*
 * @demo examples/websocket_server_test.cpp
 */

#include "HttpServer.h"
#include "WebSocketChannel.h"

struct WebSocketService {
    std::function<void(const WebSocketChannelPtr&, const std::string&)> onopen;
    std::function<void(const WebSocketChannelPtr&, const std::string&)> onmessage;
    std::function<void(const WebSocketChannelPtr&)>                     onclose;
    int ping_interval;

    WebSocketService() {
        ping_interval = 10000; // ms
    }
};
#define WebSocketServerCallbacks WebSocketService // deprecated

#define websocket_server_t      http_server_t
#define websocket_server_run    http_server_run
#define websocket_server_stop   http_server_stop

#endif // HV_WEBSOCKET_SERVER_H_
