#ifndef HV_UDP_SERVER2_HPP_
#define HV_UDP_SERVER2_HPP_

#include "hsocket.h"
#include "hlog.h"
#include <map>
#include "EventLoopThreadPool.h"
#include "Channel.h"

extern "C" void hio_handle_read(hio_t* io, void* buf, int readbytes);
extern "C" void hio_read_cb(hio_t* io, void* buf, int readbytes);
namespace hv {

template<class TSocketChannel = SocketChannel>
class UdpServerEventLoopTmpl2 {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    UdpServerEventLoopTmpl2(EventLoopPtr loop = NULL) {
        acceptor_loop = loop ? loop : std::make_shared<EventLoop>();
        listenio = NULL;
        max_connections = 0xFFFFFFFF;
        keepalive_timeout = 30000;
        load_balance = LB_RoundRobin;
    }

    virtual ~UdpServerEventLoopTmpl2() {
    }

    EventLoopPtr loop(int idx = -1) {
        return worker_threads.loop(idx);
    }
    EventLoopPtr nextLoop(sockaddr_u* addr) {
        return worker_threads.nextLoop(load_balance, addr);
    }

    //@retval >=0 listenfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        listenio = hloop_create_udp_server(acceptor_loop->loop(), host, port);
        if (!listenio) {
            hlogw("listen on %d error", port);
            return 0;
        }
        return hio_fd(listenio);
    }
    // closesocket thread-safe
    void closesocket() {
        if (listenio) {
            hio_close(listenio);
            listenio = NULL;
        }
        if(clientMap.size())
        acceptor_loop->runInLoop([this]() {
            for (auto cli : clientMap) {
                closesocket(cli.second.fd);
            }
            clientMap.clear();
        });
    }

    void setMaxConnectionNum(uint32_t num) {
        max_connections = num;
    }

    void setKeepAliveTimeout(uint32_t num) {
        keepalive_timeout = num;
    }

    void setLoadBalance(load_balance_e lb) {
        load_balance = lb;
    }

    // NOTE: totalThreadNum = 1 acceptor_thread + N worker_threads (N can be 0)
    void setThreadNum(int num) {
        worker_threads.setThreadNum(num);
    }

    int startAccept() {
        assert(listenio);
        hevent_set_userdata(listenio, this);
        hio_setcb_read(listenio, [](hio_t* io, void* buf, int len) {
            auto server = (UdpServerEventLoopTmpl2*)hevent_userdata(io);
            server->onServRecv(io, buf, len);
        });
        hio_read(listenio);
        return 0;
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        if (worker_threads.threadNum() > 0) {
            worker_threads.start(wait_threads_started);
        }
        acceptor_loop->runInLoop(std::bind(&UdpServerEventLoopTmpl2::startAccept, this));
    }
    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        if (worker_threads.threadNum() > 0) {
            worker_threads.stop(wait_threads_stopped);
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
        sockaddr_u addr = *(sockaddr_u*)hio_peeraddr(channel->io());
        acceptor_loop->runInLoop([this, addr]() {
            clientMap.erase(addr);
        });
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

    void accept(hio_t* io, EventLoopPtr loop = nullptr) {
        sockaddr_u* peerAddr = (sockaddr_u*)hio_peeraddr(io);
        int fd = hio_accept_udp_fd(io);
        if (loop == NULL)
            loop = worker_threads.nextLoop(load_balance, peerAddr);
        if (loop == NULL)
            loop = acceptor_loop;
        ++loop->connectionNum;
        clientMap[*peerAddr] = { loop , fd };
        loop->runInLoop([this, fd]() {newConnEvent(fd); });
    }
private:
    void newConnEvent(int fd) {
        if (connectionNum() >= max_connections) {
            hlogw("over max_connections");
            sockaddr_u addr;
            socklen_t addrlen = sizeof(sockaddr_u);
            getpeername(fd, &addr.sa, &addrlen);
            acceptor_loop->runInLoop([addr, this](){
                clientMap.erase(addr);
            });
            ::closesocket(fd);
            return;
        }

        // NOTE: attach to worker loop
        EventLoop* worker_loop = currentThreadEventLoop;
        assert(worker_loop != NULL);

        hio_t* connio = hio_get(worker_loop->loop(), fd);
        const TSocketChannelPtr& channel = addChannel(connio);
        channel->status = SocketChannel::CONNECTED;

        channel->onread = [this, &channel](Buffer* buf) {
            if (onMessage) {
                onMessage(channel, buf);
            }
        };
        channel->onwrite = [this, &channel](Buffer* buf) {
            if (onWriteComplete) {
                onWriteComplete(channel, buf);
            }
        };

        channel->onclose = [this, &channel]() {
            EventLoop* worker_loop = currentThreadEventLoop;
            assert(worker_loop != NULL);
            --worker_loop->connectionNum;

            channel->status = SocketChannel::CLOSED;
            if (onConnection) {
                onConnection(channel);
            }
            removeChannel(channel);
            // NOTE: After removeChannel, channel may be destroyed,
            // so in this lambda function, no code should be added below.
        };

        if (keepalive_timeout) {
            channel->setKeepaliveTimeout(keepalive_timeout);
        }
        channel->startRead();
        if (onConnection) {
            onConnection(channel);
        }
    }

    void onServRecv(hio_t* io, void* buf, int len) {
        sockaddr_u* addr = (sockaddr_u*)hio_peeraddr(io);
        auto it = clientMap.find(*addr);
        if (it == clientMap.end()) {
            char strAddr[SOCKADDR_STRLEN] = { 0 };
            SOCKADDR_STR(addr, strAddr);
            if (clientMap.size() >= max_connections) {
                hlogw("over max_connections, drop %d msg from %s", len, strAddr);
                return;
            }
            if (onNewClient) {
                HBuf tmp(buf, len);
                onNewClient(io, &tmp);
            }
            else
                accept(io);
            it = clientMap.find(*addr);
            // may lost msg
            if (it == clientMap.end()) {
                hlogd("no cliMap will drop %d msg from %s", len, strAddr);
                return;
            }
            hlogi("associate %d[%s] with thread %ld", it->second.fd, strAddr, it->second.loop->tid());
        }

        UdpCli& cli = it->second;
        if (currentThreadEventLoop != cli.loop.get()) {
            HBuf* tmp = new HBuf();
            tmp->copy(buf, len);
            // hlogi("dispatch %d msg %ld->%ld", len, hv_gettid(), cli.loop->tid());
            cli.loop->runInLoop([tmp, cli](){
                hio_t* io = hio_get(cli.loop->loop(), cli.fd);
                hio_set_readbuf(io, tmp->data(), tmp->size());
                hio_handle_read(io, tmp->data(), tmp->size());
                delete tmp;
            });
        }
        else {
            hio_t* io = hio_get(cli.loop->loop(), cli.fd);
            hio_set_readbuf(io, buf, len);
            hio_handle_read(io, buf, len);
        }
    }

public:
    hio_t*                     listenio;

    // Callback
    // run in accpet_loop, and use accept to accept udpClient
    std::function<void(hio_t*, Buffer*)>                    onNewClient;
    // the below functions run in io's loop, every channel has their own loop
    std::function<void(const TSocketChannelPtr&)>           onConnection;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    // NOTE: Use Channel::isWriteComplete in onWriteComplete callback to determine whether all data has been written.
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

    int                     keepalive_timeout;
    uint32_t                max_connections;
    load_balance_e          load_balance;

private:
    struct UdpCli {
        EventLoopPtr loop;
        int fd;
    };
    struct KeyCompare : public std::less<sockaddr_u> {
        constexpr bool operator()(const sockaddr_u& a1, const sockaddr_u& a2) const {
            return sockaddr_comp(&a1, &a2) < 0;
        }
    };
    // peerAddr -> UdpCli
    std::map<sockaddr_u, UdpCli, KeyCompare> clientMap; // GUAREDE_BY(mutex_)
    // id => TSocketChannelPtr
    std::map<uint32_t, TSocketChannelPtr>   channels; // GUAREDE_BY(mutex_)
    std::mutex                              mutex_;

    EventLoopPtr            acceptor_loop;
    EventLoopThreadPool     worker_threads;
};

template<class TSocketChannel = SocketChannel>
class UdpServerTmpl2 : private EventLoopThread, public UdpServerEventLoopTmpl2<TSocketChannel> {
public:
    UdpServerTmpl2(EventLoopPtr loop = NULL)
        : EventLoopThread()
        , UdpServerEventLoopTmpl2<TSocketChannel>(EventLoopThread::loop())
    {}
    virtual ~UdpServerTmpl2() {
        stop(true);
    }

    EventLoopPtr loop(int idx = -1) {
        return UdpServerEventLoopTmpl2<TSocketChannel>::loop(idx);
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        UdpServerEventLoopTmpl2<TSocketChannel>::start(wait_threads_started);
        EventLoopThread::start(wait_threads_started);
    }

    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        EventLoopThread::stop(wait_threads_stopped);
        UdpServerEventLoopTmpl2<TSocketChannel>::stop(wait_threads_stopped);
    }
};

typedef UdpServerTmpl2<SocketChannel> UdpServer2;

}

#endif // HV_UDP_SERVER2_HPP_
