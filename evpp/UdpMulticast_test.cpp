/*
 * UdpMulticast_test.cpp
 *
 * @build   make evpp
 * @recv    bin/UdpMulticast_test recv 239.255.0.1 4321 [local_iface]
 * @send    bin/UdpMulticast_test send 239.255.0.1 4321 [local_iface]
 *
 * Receiver: UdpServer bound to the group port, joins the multicast group, and
 *           prints datagrams. local_iface picks the receiving interface.
 * Sender:   UdpClient targeting the group address; datagrams are delivered to
 *           every group member. local_iface picks the egress interface and
 *           enables loopback so same-host receivers also get them.
 *
 * Same-host demo over the loopback interface:
 *   bin/UdpMulticast_test recv 239.1.2.3 4321 127.0.0.1
 *   bin/UdpMulticast_test send 239.1.2.3 4321 127.0.0.1
 */

#include <iostream>

#include "UdpServer.h"
#include "UdpClient.h"

using namespace hv;

static int run_recv(const char* group, int port, const char* iface) {
    auto srv = std::make_shared<UdpServer>();
    // bind to the group port on any local address, then join the group
    int bindfd = srv->createsocket(port, "0.0.0.0");
    if (bindfd < 0) {
        printf("createsocket failed: %d\n", bindfd);
        return -20;
    }
    if (udp_multicast_join(bindfd, group, iface) != 0) {
        printf("udp_multicast_join(%s) failed\n", group);
        return -21;
    }
    printf("recv: joined %s:%d, waiting for datagrams (Ctrl-C to quit)...\n", group, port);
    srv->onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("< %.*s\n", (int)buf->size(), (char*)buf->data());
    };
    srv->start();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
    }
    udp_multicast_leave(bindfd, group, iface);
    return 0;
}

static int run_send(const char* group, int port, const char* iface) {
    auto cli = std::make_shared<UdpClient>();
    int sockfd = cli->createsocket(port, group);
    if (sockfd < 0) {
        printf("createsocket failed: %d\n", sockfd);
        return -20;
    }
    if (iface) udp_multicast_set_if4(sockfd, iface);
    udp_multicast_set_loop4(sockfd, 1);  // let same-host receivers get our datagrams
    cli->start();
    printf("send: type a line to multicast to %s:%d ('quit' to exit)\n", group, port);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
        cli->sendto(line);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s recv|send group port [local_iface]\n", argv[0]);
        printf("   eg: %s recv 239.255.0.1 4321\n", argv[0]);
        printf("       %s send 239.255.0.1 4321\n", argv[0]);
        return -10;
    }
    const char* mode  = argv[1];
    const char* group = argv[2];
    int         port  = atoi(argv[3]);
    const char* iface = argc > 4 ? argv[4] : NULL;
    if (strcmp(mode, "recv") == 0) return run_recv(group, port, iface);
    if (strcmp(mode, "send") == 0) return run_send(group, port, iface);
    printf("unknown mode: %s\n", mode);
    return -11;
}
