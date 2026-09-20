/*
 * TCP client via SOCKS5 proxy.
 *
 * Demonstrates routing a TcpClient connection through a SOCKS5 proxy at the io
 * layer (hio_set_socks5). The target host is sent to the proxy as a domain name
 * (the proxy resolves it).
 *
 * @build   make examples
 * @test    # start libhv's own SOCKS5 proxy server as the proxy:
 *          bin/socks5_proxy_server 1080
 *          # then connect to an echo server through it:
 *          bin/tcp_echo_server 1234
 *          bin/socks5_client_test 127.0.0.1 1080 127.0.0.1 1234
 *
 * @example bin/socks5_client_test <proxy_host> <proxy_port> <target_host> <target_port>
 */

#include "TcpClient.h"

using namespace hv;

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: %s proxy_host proxy_port target_host target_port [user] [pass]\n", argv[0]);
        return -1;
    }
    const char* proxy_host = argv[1];
    int proxy_port = atoi(argv[2]);
    const char* target_host = argv[3];
    int target_port = atoi(argv[4]);
    const char* user = argc > 5 ? argv[5] : NULL;
    const char* pass = argc > 6 ? argv[6] : NULL;

    TcpClient cli;
    int connfd = cli.createsocket(target_port, target_host);
    if (connfd < 0) {
        printf("createsocket failed\n");
        return -1;
    }
    // route through the SOCKS5 proxy
    socks5_setting_t socks5;
    hv_strncpy(socks5.host, proxy_host, sizeof(socks5.host));
    socks5.port = proxy_port;
    if (user) hv_strncpy(socks5.username, user, sizeof(socks5.username));
    if (pass) hv_strncpy(socks5.password, pass, sizeof(socks5.password));
    cli.setSocks5Proxy(&socks5);

    cli.onConnection = [](const SocketChannelPtr& channel) {
        if (channel->isConnected()) {
            printf("connected through socks5 proxy, send hello\n");
            channel->write("hello via socks5\n");
        } else {
            printf("disconnected\n");
        }
    };
    cli.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("recv: %.*s", (int)buf->size(), (char*)buf->data());
    };

    cli.start();
    while (1) hv_sleep(1);
    return 0;
}
