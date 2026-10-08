/*
 * websocket_reconnect_test: a server that accepts TCP but rejects the
 * websocket upgrade must not reset the client's reconnect backoff.
 */

#include <atomic>
#include <cstdio>

#include "TcpServer.h"
#include "WebSocketClient.h"
#include "htime.h"

using namespace hv;

int main() {
    int port = 40834;
    TcpServer srv;
    if (srv.createsocket(port, "127.0.0.1") < 0) {
        printf("createsocket failed on port %d\n", port);
        return 1;
    }
    srv.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        channel->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    };
    srv.start();

    const int ncloses = 4;
    std::atomic<int> close_cnt{0};
    std::atomic<int> retries{-1};
    std::atomic<int> cur_delay{-1};

    WebSocketClient ws;
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 10;
    reconn.max_delay = 10000;
    reconn.delay_policy = 2;
    ws.setReconnect(&reconn);
    ws.onclose = [&]() {
        if (++close_cnt == ncloses) {
            retries = ws.reconnectRetries();
            cur_delay = ws.reconn_setting->cur_delay;
            ws.setReconnect(NULL);
        }
    };
    ws.open("ws://127.0.0.1:40834/");

    uint64_t start = gettick_ms();
    while (close_cnt < ncloses && gettick_ms() - start < 5000) {
        hv_msleep(10);
    }
    ws.stop();
    srv.stop();

    // 3 reconnects scheduled before the 4th close: delays 10, 20, 40.
    if (close_cnt >= ncloses && retries == ncloses - 1 && cur_delay == 40) {
        printf("websocket_reconnect_test PASSED\n");
        return 0;
    }
    printf("websocket_reconnect_test FAILED: closes=%d retries=%d cur_delay=%d\n",
           close_cnt.load(), retries.load(), cur_delay.load());
    return 1;
}
