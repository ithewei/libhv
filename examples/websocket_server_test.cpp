#include "WebSocketServer.h"
#include "EventLoop.h"
#include "htime.h"

using namespace hv;

int main() {
    WebSocketServerCallbacks ws;
    ws.onopen = [](const WebSocketChannelPtr& channel, const std::string& url) {
        printf("onopen: GET %s\n", url.c_str());
        // send(time) every 1s
        setInterval(1000, [channel](TimerID id) {
            if (channel->isConnected()) {
                char str[DATETIME_FMT_BUFLEN] = {0};
                datetime_t dt = datetime_now();
                datetime_fmt(&dt, str);
                channel->send(str);
            } else {
                killTimer(id);
            }
        });
    };
    ws.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) {
        printf("onmessage: %s\n", msg.c_str());
    };
    ws.onclose = [](const WebSocketChannelPtr& channel) {
        printf("onclose\n");
    };

    websocket_server_t server;
    server.port = 8888;
    server.ws = &ws;
    websocket_server_run(&server);
    return 0;
}
