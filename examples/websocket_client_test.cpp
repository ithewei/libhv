/*
 * websocket client
 *
 * @build   make examples
 * @server  bin/websocket_server_test 8888
 * @client  bin/websocket_client_test ws://127.0.0.1:8888/test
 * @clients bin/websocket_client_test ws://127.0.0.1:8888/test 100
 * @js      html/websocket_client.html
 *
 */

#include "WebSocketClient.h"

using namespace hv;

class MyWebSocketClient : public WebSocketClient {
public:
    MyWebSocketClient(EventLoopPtr loop = NULL) : WebSocketClient(loop) {}
    ~MyWebSocketClient() {}

    int connect(const char* url) {
        // set callbacks
        onopen = [this]() {
            const HttpResponsePtr& resp = getHttpResponse();
            printf("onopen\n%s\n", resp->body.c_str());
            // printf("response:\n%s\n", resp->Dump(true, true).c_str());
        };
        onmessage = [this](const std::string& msg) {
            printf("onmessage(type=%s len=%d): %.*s\n", opcode() == WS_OPCODE_TEXT ? "text" : "binary",
                (int)msg.size(), (int)msg.size(), msg.data());
        };
        onclose = []() {
            printf("onclose\n");
        };

        // reconnect: 1,2,4,8,10,10,10...
        reconn_setting_t reconn;
        reconn_setting_init(&reconn);
        reconn.min_delay = 1000;
        reconn.max_delay = 10000;
        reconn.delay_policy = 2;
        setReconnect(&reconn);

        /*
        HttpRequestPtr req = std::make_shared<HttpRequest>();
        req->method = HTTP_POST;
        req->headers["Origin"] = "http://example.com";
        req->json["app_id"] = "123456";
        req->json["app_secret"] = "abcdefg";
        printf("request:\n%s\n", req->Dump(true, true).c_str());
        setHttpRequest(req);
        */

        http_headers headers;
        headers["Origin"] = "http://example.com/";
        return open(url, headers);
    };
};
typedef std::shared_ptr<MyWebSocketClient> MyWebSocketClientPtr;

int TestMultiClientsRunInOneEventLoop(const char* url, int nclients) {
    EventLoopThreadPtr loop_thread(new EventLoopThread);
    loop_thread->start();

    std::map<int, MyWebSocketClientPtr> clients;
    for (int i = 0; i < nclients; ++i) {
        MyWebSocketClient* client = new MyWebSocketClient(loop_thread->loop());
        client->connect(url);
        clients[i] = MyWebSocketClientPtr(client);
    }

    // press Enter to stop
    while (getchar() != '\n');
    loop_thread->stop();
    loop_thread->join();

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s url\n", argv[0]);
        return -10;
    }
    const char* url = argv[1];

    int nclients = 0;
    if (argc > 2) {
        nclients = atoi(argv[2]);
    }
    if (nclients > 0) {
        return TestMultiClientsRunInOneEventLoop(url, nclients);
    }

    MyWebSocketClient ws;
    ws.connect(url);

    std::string str;
    while (std::getline(std::cin, str)) {
        if (str == "close") {
            ws.close();
        } else if (str == "open") {
            ws.connect(url);
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
