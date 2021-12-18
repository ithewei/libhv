#ifndef HV_TCP_SERVER_HPP_
#define HV_TCP_SERVER_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"

#include "EventLoopThreadPool.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
class TcpServerTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    TcpServerTmpl() {
        listenfd = -1;
        tls = false;
        enable_unpack = false;
        max_connections = 0xFFFFFFFF;
    }

    virtual ~TcpServerTmpl() {
    }

    EventLoopPtr loop(int idx = -1) {
        return worker_threads.loop(idx);
    }

    //@retval >=0 listenfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        listenfd = Listen(port, host);
        return listenfd;
    }
    // closesocket thread-safe
    void closesocket() {
        if (listenfd >= 0) {
            hio_close_async(hio_get(acceptor_thread.hloop(), listenfd));
            listenfd = -1;
        }
    }

    void setMaxConnectionNum(uint32_t num) {
        max_connections = num;
    }
    void setThreadNum(int num) {
        worker_threads.setThreadNum(num);
    }

    int startAccept() {
        assert(listenfd >= 0);
        hio_t* listenio = haccept(acceptor_thread.hloop(), listenfd, onAccept);
        hevent_set_userdata(listenio, this);
        if (tls) {
            hio_enable_ssl(listenio);
        }
        return 0;
    }

    void start(bool wait_threads_started = true) {
        worker_threads.start(wait_threads_started);
        acceptor_thread.start(wait_threads_started, std::bind(&TcpServerTmpl::startAccept, this));
    }
    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        acceptor_thread.stop(wait_threads_stopped);
        worker_threads.stop(wait_threads_stopped);
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
    const TSocketChannelPtr& addChannel(hio_t* io) {
        int fd = hio_fd(io);
        auto channel = TSocketChannelPtr(new TSocketChannel(io));
        std::lock_guard<std::mutex> locker(mutex_);
        channels[fd] = channel;
        return channels[fd];
    }

    void removeChannel(const TSocketChannelPtr& channel) {
        int fd = channel->fd();
        std::lock_guard<std::mutex> locker(mutex_);
        channels.erase(fd);
    }

    size_t connectionNum() {
        std::lock_guard<std::mutex> locker(mutex_);
        return channels.size();
    }

    int foreachChannel(std::function<void(const TSocketChannelPtr& channel)> fn) {
        std::lock_guard<std::mutex> locker(mutex_);
        for (auto& pair : channels) {
            fn(pair.second);
        }
        return channels.size();
    }

    // broadcast thread-safe
    int broadcast(const void* data, int size) {
        return foreachChannel([data, size](const TSocketChannelPtr& channel) {
            channel->write(data, size);
        });
    }

    int broadcast(const std::string& str) {
        return broadcast(str.data(), str.size());
    }

private:
    static void newConnEvent(hio_t* connio) {
        TcpServerTmpl* server = (TcpServerTmpl*)hevent_userdata(connio);
        if (server->connectionNum() >= server->max_connections) {
            hlogw("over max_connections");
            hio_close(connio);
            return;
        }

        // NOTE: attach to worker loop
        EventLoop* worker_loop = currentThreadEventLoop;
        assert(worker_loop != NULL);
        hio_attach(worker_loop->loop(), connio);

        const TSocketChannelPtr& channel = server->addChannel(connio);
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

    static void onAccept(hio_t* connio) {
        TcpServerTmpl* server = (TcpServerTmpl*)hevent_userdata(connio);
        // NOTE: detach from acceptor loop
        hio_detach(connio);
        // Load Banlance: Round-Robin
        EventLoopPtr worker_loop = server->worker_threads.nextLoop();
        worker_loop->queueInLoop(std::bind(&TcpServerTmpl::newConnEvent, connio));
    }

public:
    int                     listenfd;
    bool                    tls;
    bool                    enable_unpack;
    unpack_setting_t        unpack_setting;
    // Callback
    std::function<void(const TSocketChannelPtr&)>           onConnection;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

    uint32_t                max_connections;

private:
    // fd => TSocketChannelPtr
    std::map<int, TSocketChannelPtr> channels; // GUAREDE_BY(mutex_)
    std::mutex                       mutex_;

    EventLoopThread                 acceptor_thread;
    EventLoopThreadPool             worker_threads;
};

typedef TcpServerTmpl<SocketChannel> TcpServer;

}

#endif // HV_TCP_SERVER_HPP_
