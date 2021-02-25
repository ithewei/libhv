#ifndef HV_TCP_SERVER_HPP_
#define HV_TCP_SERVER_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"

#include "EventLoopThreadPool.h"
#include "Callback.h"
#include "Channel.h"

namespace hv {

class TcpServer {
public:
    TcpServer() {
        listenfd = -1;
        tls = false;
        max_connections = 0xFFFFFFFF;
        connection_num = 0;
    }

    virtual ~TcpServer() {
    }

    //@retval >=0 listenfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        listenfd = Listen(port, host);
        return listenfd;
    }

    void setMaxConnectionNum(uint32_t num) {
        max_connections = num;
    }
    void setThreadNum(int num) {
        loop_threads.setThreadNum(num);
    }
    void start(bool wait_threads_started = true) {
        loop_threads.start(wait_threads_started, [this](const EventLoopPtr& loop){
            assert(listenfd >= 0);
            hio_t* listenio = haccept(loop->loop(), listenfd, onAccept);
            hevent_set_userdata(listenio, this);
            if (tls) {
                hio_enable_ssl(listenio);
            }
        });
    }
    void stop(bool wait_threads_stopped = true) {
        loop_threads.stop(wait_threads_stopped);
    }

    EventLoopPtr loop(int idx = -1) {
        return loop_threads.loop(idx);
    }
    hloop_t* hloop(int idx = -1) {
        return loop_threads.hloop(idx);
    }

    int withTLS(const char* cert_file, const char* key_file) {
        tls = true;
        if (cert_file) {
            hssl_ctx_init_param_t param;
            memset(&param, 0, sizeof(param));
            param.crt_file = cert_file;
            param.key_file = key_file;
            param.endpoint = 0;
            return hssl_ctx_init(&param) == NULL ? -1 : 0;
        }
        return 0;
    }

    // channel
    const SocketChannelPtr& addChannel(hio_t* io) {
        std::lock_guard<std::mutex> locker(mutex_);
        int fd = hio_fd(io);
        if (fd >= channels.capacity()) {
            channels.resize(2 * fd);
        }
        channels[fd].reset(new SocketChannel(io));
        return channels[fd];
    }

    void removeChannel(const SocketChannelPtr& channel) {
        std::lock_guard<std::mutex> locker(mutex_);
        int fd = channel->fd();
        if (fd < channels.capacity()) {
            channels[fd] = NULL;
        }
    }

private:
    static void onAccept(hio_t* connio) {
        TcpServer* server = (TcpServer*)hevent_userdata(connio);
        if (server->connection_num >= server->max_connections) {
            hlogw("over max_connections");
            hio_close(connio);
            return;
        }
        const SocketChannelPtr& channel = server->addChannel(connio);
        channel->status = SocketChannel::CONNECTED;
        ++server->connection_num;

        channel->onread = [server, &channel](Buffer* buf) {
            if (server->onMessage) {
                server->onMessage(channel, buf);
            }
        };
        channel->onwrite = [server, &channel](Buffer* buf) {
            if (server->onWriteComplete) {
                server->onWriteComplete(channel, buf);
            }
        };
        channel->onclose = [server, &channel]() {
            channel->status = SocketChannel::CLOSED;
            if (server->onConnection) {
                server->onConnection(channel);
            }
            server->removeChannel(channel);
            --server->connection_num;
        };

        channel->startRead();
        if (server->onConnection) {
            server->onConnection(channel);
        }
    }

public:
    int                     listenfd;
    bool                    tls;
    // Callback
    ConnectionCallback      onConnection;
    MessageCallback         onMessage;
    WriteCompleteCallback   onWriteComplete;

    uint32_t                max_connections;
    std::atomic<uint32_t>   connection_num;

private:
    EventLoopThreadPool     loop_threads;
    // with fd as index
    std::vector<SocketChannelPtr>   channels; // GUAREDE_BY(mutex_)
    std::mutex                      mutex_;
};

}

#endif // HV_TCP_SERVER_HPP_
