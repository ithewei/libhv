#ifndef HV_UDP_SERVER_HPP_
#define HV_UDP_SERVER_HPP_

#include "hsocket.h"

#include "EventLoopThreadPool.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
class UdpServerEventLoopTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    UdpServerEventLoopTmpl(EventLoopPtr loop = NULL) {
        loop_ = loop ? loop : std::make_shared<EventLoop>();
        port = 0;
#if WITH_KCP
        kcp_setting = NULL;
#endif
    }

    virtual ~UdpServerEventLoopTmpl() {
#if WITH_KCP
        HV_FREE(kcp_setting);
#endif
    }

    const EventLoopPtr& loop() {
        return loop_;
    }

    //@retval >=0 bindfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        hio_t* io = hloop_create_udp_server(loop_->loop(), host, port);
        if (io == NULL) return -1;
        this->host = host;
        this->port = port;
        channel = std::make_shared<TSocketChannel>(io);
        return channel->fd();
    }
    // closesocket thread-safe
    void closesocket() {
        if (channel) {
            channel->close(true);
        }
    }

    int startRecv() {
        if (channel == NULL || channel->isClosed()) {
            int bindfd = createsocket(port, host.c_str());
            if (bindfd < 0) {
                hloge("createsocket %s:%d return %d!\n", host.c_str(), port, bindfd);
                return bindfd;
            }
        }
        if (channel == NULL || channel->isClosed()) {
            return -1;
        }
        channel->onread = [this](Buffer* buf) {
            if (onMessage) {
                onMessage(channel, buf);
            }
        };
        channel->onwrite = [this](Buffer* buf) {
            if (onWriteComplete) {
                onWriteComplete(channel, buf);
            }
        };
#if WITH_KCP
        if (kcp_setting) {
            hio_set_kcp(channel->io(), kcp_setting);
        }
#endif
        return channel->startRead();
    }

    int stopRecv() {
        if (channel == NULL) return -1;
        return channel->stopRead();
    }

    // start thread-safe
    void start() {
        loop_->runInLoop(std::bind(&UdpServerEventLoopTmpl::startRecv, this));
    }

    // sendto thread-safe
    int sendto(const void* data, int size, struct sockaddr* peeraddr = NULL) {
        if (channel == NULL) return -1;
        std::lock_guard<std::mutex> locker(sendto_mutex);
        if (peeraddr) hio_set_peeraddr(channel->io(), peeraddr, SOCKADDR_LEN(peeraddr));
        return channel->write(data, size);
    }
    int sendto(Buffer* buf, struct sockaddr* peeraddr = NULL) {
        return sendto(buf->data(), buf->size(), peeraddr);
    }
    int sendto(const std::string& str, struct sockaddr* peeraddr = NULL) {
        return sendto(str.data(), str.size(), peeraddr);
    }

#if WITH_KCP
    void setKcp(kcp_setting_t* setting) {
        if (setting == NULL) {
            HV_FREE(kcp_setting);
            return;
        }
        if (kcp_setting == NULL) {
            HV_ALLOC_SIZEOF(kcp_setting);
        }
        *kcp_setting = *setting;
    }
#endif

public:
    std::string             host;
    int                     port;
    TSocketChannelPtr       channel;
#if WITH_KCP
    kcp_setting_t*          kcp_setting;
#endif
    // Callback
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    // NOTE: Use Channel::isWriteComplete in onWriteComplete callback to determine whether all data has been written.
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

private:
    std::mutex              sendto_mutex;
    EventLoopPtr            loop_;
};

template<class TSocketChannel = SocketChannel>
class UdpServerTmpl : private EventLoopThread, public UdpServerEventLoopTmpl<TSocketChannel> {
public:
    UdpServerTmpl(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
        , UdpServerEventLoopTmpl<TSocketChannel>(EventLoopThread::loop())
        , is_loop_owner(loop == NULL)
    {}
    virtual ~UdpServerTmpl() {
        stop(true);
    }

    const EventLoopPtr& loop() {
        return EventLoopThread::loop();
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        if (isRunning()) {
            UdpServerEventLoopTmpl<TSocketChannel>::start();
        } else {
            EventLoopThread::start(wait_threads_started, std::bind(&UdpServerTmpl::startRecv, this));
        }
    }

    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        UdpServerEventLoopTmpl<TSocketChannel>::closesocket();
        if (is_loop_owner) {
            EventLoopThread::stop(wait_threads_stopped);
        }
    }

private:
    bool is_loop_owner;
};

typedef UdpServerTmpl<SocketChannel> UdpServer;

}

#endif // HV_UDP_SERVER_HPP_
