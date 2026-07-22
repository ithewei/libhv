/*
 * websocket_dns_test — integration test proving async DNS works generically
 * for WebSocketClient with NO WebSocket-specific DNS glue.
 *
 * WebSocketClient extends TcpClientTmpl, so it inherits the async hostname
 * resolution baked into TcpClientEventLoopTmpl::startConnect(). Connecting to a
 * hostname URL (ws://localhost:PORT/) must resolve via hdns without blocking
 * the loop, then complete the WS handshake.
 *
 * Uses "localhost" against a local WebSocketServer, so it needs no external
 * network (localhost resolves via /etc/hosts inside hdns).
 */

#include <atomic>
#include <cstdio>

#include "WebSocketServer.h"
#include "WebSocketClient.h"
#include "htime.h"

using namespace hv;

int main() {
    int port = 19999;

    // 1. start a local WebSocket echo server
    WebSocketService ws;
    ws.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) {
        channel->send(msg); // echo
    };

    WebSocketServer server;
    server.port = port;
    server.registerWebSocketService(&ws);
    if (server.start() != 0) {
        printf("websocket server start failed on port %d\n", port);
        return 1;
    }
    printf("websocket server started on localhost:%d\n", port);
    hv_msleep(200);

    // 2. connect via a *hostname* URL (exercises generic async DNS in the base)
    std::atomic<bool> opened{false};
    std::atomic<bool> got_echo{false};
    std::string received;

    WebSocketClient cli;
    cli.onopen = [&]() {
        printf("ws onopen\n");
        opened = true;
        cli.send("hello");
    };
    cli.onmessage = [&](const std::string& msg) {
        printf("ws onmessage: %s\n", msg.c_str());
        received = msg;
        got_echo = true;
    };
    cli.onclose = [&]() {
        printf("ws onclose\n");
    };

    char url[128];
    snprintf(url, sizeof(url), "ws://localhost:%d/", port);
    cli.open(url);

    // 3. wait for echo or timeout
    uint64_t start = gettick_ms();
    while (!got_echo && gettick_ms() - start < 6000) {
        hv_msleep(20);
    }

    cli.close();
    server.stop();
    hv_msleep(100);

    if (opened && got_echo && received == "hello") {
        printf("\nPASS: websocket connect to hostname resolved via hdns "
               "(echo=%s)\n", received.c_str());
        return 0;
    }
    printf("\nFAIL: opened=%d got_echo=%d received=%s\n",
           (int)opened, (int)got_echo, received.c_str());
    return 2;
}
