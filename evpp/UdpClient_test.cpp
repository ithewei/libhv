/*
 * UdpClient_test.cpp
 *
 * @build
 * make libhv && sudo make install
 * g++ -std=c++11 UdpClient_test.cpp -o UdpClient_test -I/usr/local/include/hv -lhv -lpthread
 *
 */

#include "UdpClient.h"
#include "htime.h"

using namespace hv;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    UdpClient cli;
    int sockfd = cli.createsocket(port);
    if (sockfd < 0) {
        return -20;
    }
    printf("client sendto port %d, sockfd=%d ...\n", port, sockfd);
    cli.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("< %.*s\n", (int)buf->size(), (char*)buf->data());
    };
    cli.onWriteComplete = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("> %.*s\n", (int)buf->size(), (char*)buf->data());
    };
    cli.start();

    // sendto(time) every 3s
    cli.loop()->setInterval(3000, [&cli](TimerID timerID) {
        char str[DATETIME_FMT_BUFLEN] = {0};
        datetime_t dt = datetime_now();
        datetime_fmt(&dt, str);
        cli.sendto(str);
    });

    while (1) hv_sleep(1);
    return 0;
}
