/*
 * dns_resolvefail_test — regression: a first-attempt DNS resolve failure must
 * still notify the user via onConnection (disconnected), even though no channel
 * existed at resolve time. Previously the notification was silently dropped
 * (channel == NULL), so a no-reconnect client never learned the connect failed.
 */

#include <atomic>
#include <cstdio>

#include "TcpClient.h"
#include "htime.h"

using namespace hv;

int main() {
    std::atomic<bool> got_disconnected_cb{false};
    std::atomic<bool> saw_connected{false};

    auto cli = std::make_shared<TcpClient>();
    // hostname that cannot resolve (RFC 6761 .invalid), NO reconnect configured.
    cli->createsocket(80, "definitely-nonexistent.invalid");
    cli->onConnection = [&](const SocketChannelPtr& channel) {
        // must receive a valid (non-null) channel; user code commonly calls
        // channel->isConnected() / peeraddr() first, so nullptr would crash.
        if (channel && channel->isConnected()) {
            saw_connected = true;
        } else {
            // exercise the channel API to ensure a NULL-io channel is safe
            (void)channel->peeraddr();
            got_disconnected_cb = true;
        }
    };
    cli->start();

    // wait for the resolve to fail and the callback to be delivered
    uint64_t start = gettick_ms();
    while (!got_disconnected_cb && gettick_ms() - start < 8000) {
        hv_msleep(20);
    }

    cli->stop();
    hv_msleep(100);

    if (got_disconnected_cb && !saw_connected) {
        printf("\nPASS: first-attempt resolve failure notifies onConnection(disconnected)\n");
        return 0;
    }
    printf("\nFAIL: got_disconnected_cb=%d saw_connected=%d\n",
           (int)got_disconnected_cb, (int)saw_connected);
    return 1;
}
