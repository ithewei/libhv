#include <cstdio>
#include <cstdlib>
#include <string>

#include "WebSocketClient.h"
#include "WebSocketServer.h"

using namespace hv;

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s failed\n", __LINE__, #expr); \
    std::exit(1); \
} } while (0)

int main() {
    WebSocketService peer;
    peer.onmessage = [](const WebSocketChannelPtr& ch, const std::string& msg) {
        ch->send("ready");
        if (msg == "local") {
            ch->send(std::string("\x03\xe8"), WS_OPCODE_CLOSE);
        } else if (msg == "coded") {
            // Application code 4000 in network byte order, followed by a reason.
            ch->send(std::string("\x0f\xa0maintenance"), WS_OPCODE_CLOSE);
        } else if (msg == "empty") {
            ch->send(std::string(), WS_OPCODE_CLOSE);
        } else {
            CHECK(msg == "drop");
            ch->close();
        }
    };
    WebSocketServer server(&peer);
    server.setHost("127.0.0.1");
    server.setPort(0);
    CHECK(server.start() == 0);
    std::string url = "ws://127.0.0.1:" + std::to_string(server.port) + "/";

    struct {
        const char* command;
        int code;
    } cases[] = {
        {"local", WebSocketClient::CloseCode::LocalClose},
        {"drop",  WebSocketClient::CloseCode::NoCloseFrame},
        {"coded", 4000},
        {"empty", WebSocketClient::CloseCode::NoCloseStatus},
        {"coded", 4000},
    };
    const size_t count = sizeof(cases) / sizeof(cases[0]);
    size_t closed = 0;
    std::string events;
    char readbuf[64]; // valid until the parser finishes, including after close()
    EventLoopPtr loop = std::make_shared<EventLoop>();
    WebSocketClient client(loop);
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = reconn.max_delay = 10;
    auto connect = [&]() {
        client.setReconnect(&reconn);
        CHECK(client.open(url.c_str()) == 0);
    };
    client.onopen = [&]() {
        CHECK(closed < count);
        CHECK(client.getCloseCode() == WebSocketClient::CloseCode::NoCloseFrame);
        events += "open ";
        if (closed == 0) {
            // Deliver TEXT and CLOSE in one parser call. The TEXT callback
            // closes the client before the parser reaches the peer's CLOSE.
            int bytes = ws_calc_frame_size(5) + ws_calc_frame_size(2);
            client.channel->setReadBuf(readbuf, sizeof(readbuf));
            CHECK(client.channel->readBytes(bytes) >= 0);
        }
        client.send(cases[closed].command);
    };
    client.onmessage = [&](const std::string& msg) {
        CHECK(closed < count && msg == "ready");
        events += "message ";
        if (closed == 0) client.close();
    };
    client.onclose = [&]() {
        CHECK(!client.isConnected());
        if (closed == count) {
            // The server stopped after the last coded CLOSE. The retry failed
            // before onopen, so it must not report the previous peer's code.
            CHECK(events.empty());
            CHECK(client.getCloseCode() == WebSocketClient::CloseCode::NoCloseFrame);
            client.setReconnect(NULL);
            loop->stop();
            return;
        }
        CHECK(events == "open message ");
        CHECK(client.getCloseCode() == cases[closed].code);
        events.clear();
        ++closed;
        if (closed == 1) {
            // Local close disables automatic reconnect. Reopen the same client.
            loop->queueInLoop(connect);
        } else if (closed == count) {
            server.stop();
        }
    };

    loop->queueInLoop(connect);
    loop->setTimeout(5000, [](TimerID) {
        std::fprintf(stderr, "timed out waiting for WebSocket callbacks\n");
        std::exit(1);
    });
    loop->run();
    return 0;
}
