/*
 * websocket client
 *
 * @build   make examples
 * @server  bin/websocket_server_test 8888
 * @client  bin/websocket_client_test ws://127.0.0.1:8888/
 * @js      html/websocket_client.html
 *
 */

#include "WebSocketClient.h"

using namespace hv;

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s url\n", argv[0]);
        return -10;
    }
    const char* url = argv[1];

    WebSocketClient ws;
    ws.onopen = []() {
        printf("onopen\n");
    };
    ws.onclose = []() {
        printf("onclose\n");
    };
    ws.onmessage = [](const std::string& msg) {
        printf("onmessage: %s\n", msg.c_str());
    };

    // reconnect: 1,2,4,8,10,10,10...
    ReconnectInfo reconn;
    reconn.min_delay = 1000;
    reconn.max_delay = 10000;
    reconn.delay_policy = 2;
    ws.setReconnect(&reconn);

    http_headers headers;
    headers["Origin"] = "http://example.com/";
    ws.open(url, headers);

    std::string str;
    while (std::getline(std::cin, str)) {
        if (!ws.isConnected()) break;
        if (str == "quit") {
            ws.close();
            break;
        }
        ws.send(str);
    }

    return 0;
}
