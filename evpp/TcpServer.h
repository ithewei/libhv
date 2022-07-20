#ifndef HV_TCP_SERVER_HPP_
#define HV_TCP_SERVER_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"

#include "EventLoopThreadPool.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
class TcpServerEventLoopTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    TcpServerEventLoopTmpl(EventLoopPtr loop = NULL) {
        acceptor_loop = loop ? loop : std::make_shared<EventLoop>();
        listenfd = -1;
        tls = false;
        unpack_setting.mode = UNPACK_MODE_NONE;
        max_connections = 0xFFFFFFFF;
        load_balance = LB_RoundRobin;
    }

    virtual ~TcpServerEventLoopTmpl() {
    }

    EventLoopPtr loop(int idx = -1) {
        return worker_threads.loop(idx);
    }

    //@retval >=0 listenfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        listenfd = Listen(port, host);
        if (listenfd < 0) return listenfd;
        this->host = host;
        this->port = port;
        return listenfd;
    }
    // closesocket thread-safe
    void closesocket() {
        if (listenfd >= 0) {
            hloop_t* loop = acceptor_loop->loop();
            if (loop) {
                hio_t* listenio = hio_get(loop, listenfd);
                assert(listenio != NULL);
                hio_close_async(listenio);
            }
            listenfd = -1;
        }
    }

    void setMaxConnectionNum(uint32_t num) {
        max_connections = num;
    }

    void setLoadBalance(load_balance_e lb) {
        load_balance = lb;
    }

    // NOTE: totalThreadNum = 1 acceptor_thread + N worker_threads (N can be 0)
    void setThreadNum(int num) {
        worker_threads.setThreadNum(num);
    }

    int startAccept() {
        if (listenfd < 0) {
            listenfd = createsocket(port, host.c_str());
            if (listenfd < 0) {
                hloge("createsocket %s:%d return %d!\n", host.c_str(), port, listenfd);
                return listenfd;
            }
        }
        hloop_t* loop = acceptor_loop->loop();
        if (loop == NULL) return -2;
        hio_t* listenio = haccept(loop, listenfd, onAccept);
        assert(listenio != NULL);
        hevent_set_userdata(listenio, this);
        if (tls) {
            hio_enable_ssl(listenio);
        }
        return 0;
    }

    int stopAccept() {
        if (listenfd < 0) return -1;
        hloop_t* loop = acceptor_loop->loop();
        if (loop == NULL) return -2;
        hio_t* listenio = hio_get(loop, listenfd);
        assert(listenio != NULL);
        return hio_del(listenio, HV_READ);
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        if (worker_threads.threadNum() > 0) {
            worker_threads.start(wait_threads_started);
        }
        acceptor_loop->runInLoop(std::bind(&TcpServerEventLoopTmpl::startAccept, this));
    }
    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        closesocket();
        if (worker_threads.threadNum() > 0) {
            worker_threads.stop(wait_threads_stopped);
        }
    }

    int withTLS(hssl_ctx_opt_t* opt = NULL) {
        tls = true;
        if (opt) {
            opt->endpoint = HSSL_SERVER;
            if (hssl_ctx_init(opt) == NULL) {
                fprintf(stderr, "hssl_ctx_init failed!\n");
                return -1;
            }
        }
        return 0;
    }

    void setUnpack(unpack_setting_t* setting) {
        if (setting) {
            unpack_setting = *setting;
        } else {
            unpack_setting.mode = UNPACK_MODE_NONE;
        }
    }

    // channel
    const TSocketChannelPtr& addChannel(hio_t* io) {
        uint32_t id = hio_id(io);
        auto channel = TSocketChannelPtr(new TSocketChannel(io));
        std::lock_guard<std::mutex> locker(mutex_);
        channels[id] = channel;
        return channels[id];
    }

    TSocketChannelPtr getChannelById(uint32_t id) {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = channels.find(id);
        return iter != channels.end() ? iter->second : NULL;
    }

    void removeChannel(const TSocketChannelPtr& channel) {
        uint32_t id = channel->id();
        std::lock_guard<std::mutex> locker(mutex_);
        channels.erase(id);
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
        TcpServerEventLoopTmpl* server = (TcpServerEventLoopTmpl*)hevent_userdata(connio);
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
            EventLoop* worker_loop = currentThreadEventLoop;
            assert(worker_loop != NULL);
            --worker_loop->connectionNum;

            channel->status = SocketChannel::CLOSED;
            if (server->onConnection) {
                server->onConnection(channel);
            }
            server->removeChannel(channel);
            // NOTE: After removeChannel, channel may be destroyed,
            // so in this lambda function, no code should be added below.
        };

        if (server->unpack_setting.mode != UNPACK_MODE_NONE) {
            channel->setUnpack(&server->unpack_setting);
        }
        channel->startRead();
        if (server->onConnection) {
            server->onConnection(channel);
        }
    }

    static void onAccept(hio_t* connio) {
        TcpServerEventLoopTmpl* server = (TcpServerEventLoopTmpl*)hevent_userdata(connio);
        // NOTE: detach from acceptor loop
        hio_detach(connio);
        EventLoopPtr worker_loop = server->worker_threads.nextLoop(server->load_balance);
        if (worker_loop == NULL) {
            worker_loop = server->acceptor_loop;
        }
        ++worker_loop->connectionNum;
        worker_loop->runInLoop(std::bind(&TcpServerEventLoopTmpl::newConnEvent, connio));
    }

public:
    std::string             host;
    int                     port;
    int                     listenfd;
    bool                    tls;
    unpack_setting_t        unpack_setting;
    // Callback
    std::function<void(const TSocketChannelPtr&)>           onConnection;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    // NOTE: Use Channel::isWriteComplete in onWriteComplete callback to determine whether all data has been written.
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

    uint32_t                max_connections;
    load_balance_e          load_balance;

private:
    // id => TSocketChannelPtr
    std::map<uint32_t, TSocketChannelPtr>   channels; // GUAREDE_BY(mutex_)
    std::mutex                              mutex_;

    EventLoopPtr            acceptor_loop;
    EventLoopThreadPool     worker_threads;
};

template<class TSocketChannel = SocketChannel>
class TcpServerTmpl : private EventLoopThread, public TcpServerEventLoopTmpl<TSocketChannel> {
public:
    TcpServerTmpl(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
        , TcpServerEventLoopTmpl<TSocketChannel>(EventLoopThread::loop())
    {}
    virtual ~TcpServerTmpl() {
        stop(true);
    }

    const EventLoopPtr& loop(int idx = -1) {
        return TcpServerEventLoopTmpl<TSocketChannel>::loop(idx);
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        TcpServerEventLoopTmpl<TSocketChannel>::start(wait_threads_started);
        EventLoopThread::start(wait_threads_started);
    }

    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        EventLoopThread::stop(wait_threads_stopped);
        TcpServerEventLoopTmpl<TSocketChannel>::stop(wait_threads_stopped);
    }
};

typedef TcpServerTmpl<SocketChannel> TcpServer;

}

#endif // HV_TCP_SERVER_HPP_
