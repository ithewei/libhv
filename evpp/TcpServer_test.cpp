/*
 * TcpServer_test.cpp
 *
 * @build: make evpp
 *
 */

#include "TcpServer.h"

using namespace hv;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    hlog_set_level(LOG_LEVEL_DEBUG);

    TcpServer srv;
    int listenfd = srv.createsocket(port);
    if (listenfd < 0) {
        return -20;
    }
    printf("server listen on port %d, listenfd=%d ...\n", port, listenfd);
    srv.onConnection = [](const SocketChannelPtr& channel) {
        std::string peeraddr = channel->peeraddr();
        if (channel->isConnected()) {
            printf("%s connected! connfd=%d tid=%ld\n", peeraddr.c_str(), channel->fd(), currentThreadEventLoop->tid());
        } else {
            printf("%s disconnected! connfd=%d tid=%ld\n", peeraddr.c_str(), channel->fd(), currentThreadEventLoop->tid());
        }
    };
    srv.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        // echo
        printf("< %.*s\n", (int)buf->size(), (char*)buf->data());
        channel->write(buf);
    };
    srv.setThreadNum(4);
    srv.start();

    // press Enter to stop
    while (getchar() != '\n');

    return 0;
}
