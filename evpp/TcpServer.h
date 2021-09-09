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
        enable_unpack = false;
        max_connections = 0xFFFFFFFF;
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
        if (cert_file) {
            hssl_ctx_init_param_t param;
            memset(&param, 0, sizeof(param));
            param.crt_file = cert_file;
            param.key_file = key_file;
            param.endpoint = HSSL_SERVER;
            if (hssl_ctx_init(&param) == NULL) {
                fprintf(stderr, "hssl_ctx_init failed!\n");
                return -1;
            }
        }
        tls = true;
        return 0;
    }

    void setUnpack(unpack_setting_t* setting) {
        if (setting) {
            enable_unpack = true;
            unpack_setting = *setting;
        } else {
            enable_unpack = false;
        }
    }

    // channel
    const SocketChannelPtr& addChannel(hio_t* io) {
        int fd = hio_fd(io);
        auto channel = SocketChannelPtr(new SocketChannel(io));
        std::lock_guard<std::mutex> locker(mutex_);
        channels[fd] = channel;
        return channels[fd];
    }

    void removeChannel(const SocketChannelPtr& channel) {
        int fd = channel->fd();
        std::lock_guard<std::mutex> locker(mutex_);
        channels.erase(fd);
    }

    size_t connectionNum() {
        std::lock_guard<std::mutex> locker(mutex_);
        return channels.size();
    }

private:
    static void onAccept(hio_t* connio) {
        TcpServer* server = (TcpServer*)hevent_userdata(connio);
        if (server->connectionNum() >= server->max_connections) {
            hlogw("over max_connections");
            hio_close(connio);
            return;
        }
        const SocketChannelPtr& channel = server->addChannel(connio);
        channel->status = SocketChannel::CONNECTED;

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
            // NOTE: After removeChannel, channel may be destroyed,
            // so in this lambda function, no code should be added below.
        };

        if (server->enable_unpack) {
            channel->setUnpack(&server->unpack_setting);
        }
        channel->startRead();
        if (server->onConnection) {
            server->onConnection(channel);
        }
    }

public:
    int                     listenfd;
    bool                    tls;
    bool                    enable_unpack;
    unpack_setting_t        unpack_setting;
    // Callback
    ConnectionCallback      onConnection;
    MessageCallback         onMessage;
    WriteCompleteCallback   onWriteComplete;

    uint32_t                max_connections;

private:
    EventLoopThreadPool     loop_threads;
    // fd => SocketChannelPtr
    std::map<int, SocketChannelPtr> channels; // GUAREDE_BY(mutex_)
    std::mutex                      mutex_;
};

}

#endif // HV_TCP_SERVER_HPP_
