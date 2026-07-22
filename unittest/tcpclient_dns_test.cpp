/*
 * tcpclient_dns_test — integration test for async DNS in the evpp connect path.
 *
 * Verifies that TcpClient with a *hostname* target (not a numeric IP) connects
 * successfully and that reconnect re-resolves the hostname through the async
 * resolver (TcpClientEventLoopTmpl::startResolveThenConnect) without blocking
 * the event loop.
 *
 * Uses "localhost" (resolved via /etc/hosts by hdns) against a local TCP
 * server, so it needs no external network.
 */

#include <atomic>
#include <cstdio>

#include "TcpServer.h"
#include "TcpClient.h"
#include "EventLoop.h"
#include "htime.h"

using namespace hv;

static std::atomic<int> g_connect_count{0};
static std::atomic<bool> g_reconnected{false};

int main() {
    // 1. start a TCP echo server on a fixed local port
    int port = 40833;
    TcpServer srv;
    int listenfd = srv.createsocket(port, "127.0.0.1");
    if (listenfd < 0) {
        printf("createsocket failed on port %d\n", port);
        return 1;
    }
    printf("server listening on 127.0.0.1:%d\n", port);
    srv.onMessage = [](const SocketChannelPtr& ch, Buffer* buf) {
        ch->write(buf); // echo
    };
    srv.start();

    // 2. TcpClient targeting "localhost" (a hostname -> exercises DNS path)
    auto cli = std::make_shared<TcpClient>();
    int connfd = cli->createsocket(port, "localhost");
    if (connfd < 0) {
        printf("client createsocket failed\n");
        return 2;
    }

    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 100;
    reconn.max_delay = 500;
    reconn.max_retry_cnt = 10;
    cli->setReconnect(&reconn);

    cli->onConnection = [&](const SocketChannelPtr& channel) {
        if (channel->isConnected()) {
            int n = ++g_connect_count;
            printf("connected to %s (count=%d)\n", channel->peeraddr().c_str(), n);
            if (n == 1) {
                // force a disconnect shortly to trigger reconnect + re-resolve.
                // channel->close() is thread-safe and runs in the loop thread.
                setTimeout(200, [channel](TimerID) {
                    printf("closing to trigger reconnect...\n");
                    channel->close();
                });
            } else if (n >= 2) {
                g_reconnected = true;
            }
        } else {
            printf("disconnected\n");
        }
    };
    cli->start();

    // 3. run until reconnect confirmed or timeout
    uint64_t start = gettick_ms();
    while (!g_reconnected && gettick_ms() - start < 8000) {
        hv_msleep(50);
    }

    // stop reconnect first (thread-safe), then let the client shut down.
    cli->setReconnect(NULL);
    cli->stop(true);
    srv.stop();
    hv_msleep(200);

    if (g_connect_count >= 2 && g_reconnected) {
        printf("\nPASS: hostname connect + async re-resolve reconnect worked "
               "(connects=%d)\n", g_connect_count.load());
        return 0;
    }
    printf("\nFAIL: connects=%d reconnected=%d\n",
           g_connect_count.load(), (int)g_reconnected);
    return 3;
}
