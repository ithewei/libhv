// @see muduo/examples/simple/echo
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/TcpServer.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

using muduo::Timestamp;

using muduo::net::EventLoop;
using muduo::net::InetAddress;
using muduo::net::TcpServer;
using muduo::net::TcpConnectionPtr;
using muduo::net::Buffer;

class EchoTcpServer {
public:
    EchoTcpServer(EventLoop* loop, const InetAddress& addr)
        : server_(loop, addr, "EchoTcpServer")
    {
        server_.setConnectionCallback(std::bind(&EchoTcpServer::onConnection, this, _1));
        server_.setMessageCallback(std::bind(&EchoTcpServer::onMessage, this, _1, _2, _3));
    }

    void start() {
        server_.start();
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
    }

    void onMessage(const TcpConnectionPtr& conn,
            Buffer* buf, Timestamp time) {
        muduo::string msg(buf->retrieveAllAsString());
        conn->send(msg);
    }

    TcpServer server_;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    muduo::g_logLevel = muduo::Logger::ERROR;
    muduo::net::EventLoop loop;

    muduo::net::InetAddress addr(port);
    EchoTcpServer server(&loop, addr);
    server.start();

    loop.loop();

    return 0;
}
