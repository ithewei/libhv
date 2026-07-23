/*
 * dns_lifetime_test — regression test for the DNS handle lifetime redesign.
 *
 * Destroys a TcpClient while a hostname DNS resolve is still in flight. With the
 * old raw-pointer handle this could double-free / use-after-free (the loop's
 * resolver frees the query at teardown while the client still held the pointer,
 * or a resolve callback fired into a destroyed client). With the DnsID model
 * (EventLoop keeps a DnsID -> query map, stale ids no-op) this must be clean.
 *
 * The client targets a hostname pointed at a black-hole nameserver so the
 * resolve stays in flight; we then destroy the client immediately.
 */

#include <cstdio>
#include <memory>

#include "TcpClient.h"
#include "htime.h"

using namespace hv;

int main() {
    // Run several iterations to shake out races/UAF under repeated teardown.
    for (int i = 0; i < 20; ++i) {
        auto cli = std::make_shared<TcpClient>();
        // A hostname (not numeric IP) forces the async DNS path. Use a .invalid
        // TLD (RFC 6761: guaranteed not to resolve) so the query stays in flight
        // / fails, never connecting.
        cli->createsocket(80, "nonexistent-host.invalid");
        cli->start();
        // Give the resolve just enough time to be issued but not complete,
        // then destroy the client while it's (very likely) still in flight.
        hv_msleep(i % 3); // 0/1/2 ms: vary the teardown window
        cli->stop();
        cli.reset(); // destroy client mid-resolve
    }

    // Also exercise immediate destroy with zero delay (resolve definitely in flight).
    for (int i = 0; i < 20; ++i) {
        auto cli = std::make_shared<TcpClient>();
        cli->createsocket(80, "another-missing-host.invalid");
        cli->start();
        cli->stop();
        cli.reset();
    }

    printf("\nPASS: destroying TcpClient mid-resolve is clean (no UAF/double-free)\n");
    return 0;
}
