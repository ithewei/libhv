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
    ws.onmessage = [&ws](const std::string& msg) {
        printf("onmessage(type=%s len=%d): %.*s\n", ws.opcode() == WS_OPCODE_TEXT ? "text" : "binary",
            (int)msg.size(), (int)msg.size(), msg.data());
    };
    ws.onclose = []() {
        printf("onclose\n");
    };

    // reconnect: 1,2,4,8,10,10,10...
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 1000;
    reconn.max_delay = 10000;
    reconn.delay_policy = 2;
    ws.setReconnect(&reconn);

    http_headers headers;
    headers["Origin"] = "http://example.com/";
    ws.open(url, headers);

    std::string str;
    while (std::getline(std::cin, str)) {
        if (str == "close") {
            ws.close();
        } else if (str == "open") {
            ws.open(url, headers);
        } else if (str == "stop") {
            ws.stop();
            break;
        } else {
            if (!ws.isConnected()) break;
            ws.send(str);
        }
    }

    return 0;
}
