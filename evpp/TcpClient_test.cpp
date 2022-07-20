/*
 * TcpClient_test.cpp
 *
 * @build   make evpp
 * @server  bin/TcpServer_test 1234
 * @client  bin/TcpClient_test 1234
 *
 */

#include <iostream>

#include "TcpClient.h"
#include "htime.h"

#define TEST_RECONNECT  1
#define TEST_TLS        0

using namespace hv;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    TcpClient cli;
    int connfd = cli.createsocket(port);
    if (connfd < 0) {
        return -20;
    }
    printf("client connect to port %d, connfd=%d ...\n", port, connfd);
    cli.onConnection = [&cli](const SocketChannelPtr& channel) {
        std::string peeraddr = channel->peeraddr();
        if (channel->isConnected()) {
            printf("connected to %s! connfd=%d\n", peeraddr.c_str(), channel->fd());
            // send(time) every 3s
            setInterval(3000, [channel](TimerID timerID){
                if (channel->isConnected()) {
                    if (channel->isWriteComplete()) {
                        char str[DATETIME_FMT_BUFLEN] = {0};
                        datetime_t dt = datetime_now();
                        datetime_fmt(&dt, str);
                        channel->write(str);
                    }
                } else {
                    killTimer(timerID);
                }
            });
        } else {
            printf("disconnected to %s! connfd=%d\n", peeraddr.c_str(), channel->fd());
        }
        if (cli.isReconnect()) {
            printf("reconnect cnt=%d, delay=%d\n", cli.reconn_setting->cur_retry_cnt, cli.reconn_setting->cur_delay);
        }
    };
    cli.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("< %.*s\n", (int)buf->size(), (char*)buf->data());
    };

#if TEST_RECONNECT
    // reconnect: 1,2,4,8,10,10,10...
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 1000;
    reconn.max_delay = 10000;
    reconn.delay_policy = 2;
    cli.setReconnect(&reconn);
#endif

#if TEST_TLS
    cli.withTLS();
#endif

    cli.start();

    std::string str;
    while (std::getline(std::cin, str)) {
        if (str == "close") {
            cli.closesocket();
        } else if (str == "start") {
            cli.start();
        } else if (str == "stop") {
            cli.stop();
            break;
        } else {
            if (!cli.isConnected()) break;
            cli.send(str);
        }
    }

    return 0;
}
