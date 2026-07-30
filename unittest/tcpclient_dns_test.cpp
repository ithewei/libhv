/*
 * tcpclient_dns_test — integration tests for async DNS in the evpp connect path.
 *
 * Covers three scenarios (all no external network required):
 *   1. connect: TcpClient with a *hostname* target ("localhost", resolved via
 *      /etc/hosts) connects, and reconnect re-resolves the hostname through the
 *      async resolver without blocking the loop.
 *   2. lifetime: destroying a TcpClient while a hostname resolve is still in
 *      flight must be clean (no use-after-free / double-free). This exercises
 *      the DnsID handle model (EventLoop keeps a DnsID -> query map; a stale id
 *      no-ops on cancel).
 *   3. resolvefail: a first-attempt resolve failure (before any channel exists)
 *      must still notify onConnection with a valid, non-connected channel whose
 *      error() reports ERR_DNS_RESOLVE.
 */

#include <atomic>
#include <cstdio>
#include <memory>

#include "TcpServer.h"
#include "TcpClient.h"
#include "EventLoop.h"
#include "herr.h"
#include "htime.h"

using namespace hv;

// 1. hostname connect + async re-resolve on reconnect
static int test_connect_and_reconnect() {
    std::atomic<int> connect_count{0};
    std::atomic<bool> reconnected{false};

    int port = 40833;
    TcpServer srv;
    int listenfd = srv.createsocket(port, "127.0.0.1");
    if (listenfd < 0) {
        printf("[connect] createsocket failed on port %d\n", port);
        return -1;
    }
    srv.onMessage = [](const SocketChannelPtr& ch, Buffer* buf) {
        ch->write(buf); // echo
    };
    srv.start();

    // TcpClient targeting "localhost" (a hostname -> exercises DNS path)
    auto cli = std::make_shared<TcpClient>();
    int connfd = cli->createsocket(port, "localhost");
    if (connfd < 0) {
        printf("[connect] client createsocket failed\n");
        return -1;
    }

    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 100;
    reconn.max_delay = 500;
    reconn.max_retry_cnt = 10;
    cli->setReconnect(&reconn);

    cli->onConnection = [&](const SocketChannelPtr& channel) {
        if (channel->isConnected()) {
            int n = ++connect_count;
            printf("[connect] connected to %s (count=%d)\n", channel->peeraddr().c_str(), n);
            if (n == 1) {
                // force a disconnect shortly to trigger reconnect + re-resolve.
                setTimeout(200, [channel](TimerID) {
                    channel->close();
                });
            } else if (n >= 2) {
                reconnected = true;
            }
        }
    };
    cli->start();

    uint64_t start = gettick_ms();
    while (!reconnected && gettick_ms() - start < 8000) {
        hv_msleep(50);
    }

    cli->setReconnect(NULL);
    cli->stop(true);
    srv.stop();
    hv_msleep(200);

    if (connect_count >= 2 && reconnected) {
        printf("[connect] PASS: hostname connect + async re-resolve reconnect (connects=%d)\n",
               connect_count.load());
        return 0;
    }
    printf("[connect] FAIL: connects=%d reconnected=%d\n",
           connect_count.load(), (int)reconnected);
    return -1;
}

// 2. destroying a client mid-resolve must not UAF / double-free
static int test_destroy_mid_resolve() {
    // Repeated teardown while a hostname resolve is (very likely) still in flight.
    // Use .invalid (RFC 6761: never resolves) so the query stays pending/fails.
    for (int i = 0; i < 20; ++i) {
        auto cli = std::make_shared<TcpClient>();
        cli->createsocket(80, "nonexistent-host.invalid");
        cli->start();
        hv_msleep(i % 3); // vary the teardown window: 0/1/2 ms
        cli->stop();
        cli.reset(); // destroy client mid-resolve
    }
    for (int i = 0; i < 20; ++i) {
        auto cli = std::make_shared<TcpClient>();
        cli->createsocket(80, "another-missing-host.invalid");
        cli->start();
        cli->stop();
        cli.reset();
    }
    printf("[lifetime] PASS: destroying TcpClient mid-resolve is clean (no UAF/double-free)\n");
    return 0;
}

// 3. first-attempt resolve failure must notify onConnection with ERR_DNS_RESOLVE
static int test_resolve_failure_notifies() {
    std::atomic<bool> got_disconnected_cb{false};
    std::atomic<bool> saw_connected{false};
    std::atomic<int> reported_error{0};

    auto cli = std::make_shared<TcpClient>();
    // unresolvable hostname (RFC 6761 .invalid), NO reconnect configured.
    cli->createsocket(80, "definitely-nonexistent.invalid");
    cli->onConnection = [&](const SocketChannelPtr& channel) {
        // user code commonly calls channel->isConnected()/peeraddr() first,
        // so a nullptr channel would crash; a NULL-io channel must be safe.
        if (channel && channel->isConnected()) {
            saw_connected = true;
        } else {
            (void)channel->peeraddr();          // must be safe on a NULL-io channel
            reported_error = channel->error();  // must be ERR_DNS_RESOLVE
            got_disconnected_cb = true;
        }
    };
    cli->start();

    uint64_t start = gettick_ms();
    while (!got_disconnected_cb && gettick_ms() - start < 8000) {
        hv_msleep(20);
    }
    cli->stop();
    hv_msleep(100);

    if (got_disconnected_cb && !saw_connected && reported_error == ERR_DNS_RESOLVE) {
        printf("[resolvefail] PASS: resolve failure notifies onConnection (error=%d %s)\n",
               reported_error.load(), hv_strerror(reported_error));
        return 0;
    }
    printf("[resolvefail] FAIL: got_cb=%d connected=%d error=%d (want %d)\n",
           (int)got_disconnected_cb, (int)saw_connected,
           reported_error.load(), ERR_DNS_RESOLVE);
    return -1;
}

int main() {
    int rc = 0;
    rc |= test_connect_and_reconnect();
    rc |= test_destroy_mid_resolve();
    rc |= test_resolve_failure_notifies();

    if (rc == 0) {
        printf("\nALL tcpclient_dns_test PASSED\n");
        return 0;
    }
    printf("\ntcpclient_dns_test FAILED\n");
    return 1;
}
