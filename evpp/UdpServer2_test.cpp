/*
 * TcpServer_test.cpp
 *
 * @build   make evpp
 * @server  bin/UdpServer2_test 1234
 * @client  bin/UdpClient_test 1234
 *
 */

#include "UdpServer2.h"

using namespace hv;


int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    hlog_set_level(LOG_LEVEL_DEBUG);
    hlog_set_handler(stdout_logger);
    UdpServer2 srv;
    int listenfd = srv.createsocket(port);
    if (listenfd < 0) {
        return -20;
    }
    printf("server listen on port %d, listenfd=%d ...\n", port, listenfd);

    srv.onNewClient = [&srv](hio_t* io, Buffer* data) {
        // @todo select loop with data or peerAddr
        EventLoopPtr loop = srv.loop();
        srv.accept(io, loop);
    };
    srv.onConnection = [&srv](const SocketChannelPtr& channel) {
        char msg[256];
        std::string peeraddr = channel->peeraddr();
        if (channel->isConnected()) {
            sprintf(msg, "%s connected! connfd=%d id=%d tid=%ld\n", peeraddr.c_str(), channel->fd(), channel->id(), currentThreadEventLoop->tid());
            srv.broadcast(msg);
            printf(msg);
        } else {
            sprintf(msg, "%s disconnected! connfd=%d id=%d tid=%ld\n", peeraddr.c_str(), channel->fd(), channel->id(), currentThreadEventLoop->tid());
            srv.broadcast(msg);
            printf(msg);
        }
    };
    srv.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        // echo
        printf("%d< %.*s\n", channel->id(), (int)buf->size(), (char*)buf->data());
        channel->write(buf);
    };
    srv.onWriteComplete = [](const SocketChannelPtr& channel, Buffer* buf) {
        printf("%d> %.*s\n", channel->id(), (int)buf->size(), (char*)buf->data());
    };
    srv.setThreadNum(4);
    srv.setLoadBalance(LB_LeastConnections);

    srv.start();

    // press Enter to stop
    while (getchar() != '\n');

    return 0;
}
