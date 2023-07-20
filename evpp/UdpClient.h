#ifndef HV_UDP_CLIENT_HPP_
#define HV_UDP_CLIENT_HPP_

#include "hsocket.h"

#include "EventLoopThread.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
class UdpClientEventLoopTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    UdpClientEventLoopTmpl(EventLoopPtr loop = NULL) {
        loop_ = loop ? loop : std::make_shared<EventLoop>();
        remote_port = 0;
#if WITH_KCP
        kcp_setting = NULL;
#endif
    }

    virtual ~UdpClientEventLoopTmpl() {
#if WITH_KCP
        HV_FREE(kcp_setting);
#endif
    }

    const EventLoopPtr& loop() {
        return loop_;
    }

    // NOTE: By default, not bind local port. If necessary, you can call bind() after createsocket().
    // @retval >=0 sockfd, <0 error
    int createsocket(int remote_port, const char* remote_host = "127.0.0.1") {
        hio_t* io = hloop_create_udp_client(loop_->loop(), remote_host, remote_port);
        if (io == NULL) return -1;
        this->remote_host = remote_host;
        this->remote_port = remote_port;
        channel = std::make_shared<TSocketChannel>(io);
        int sockfd = channel->fd();
        if (hv_strendswith(remote_host, ".255")) {
            udp_broadcast(sockfd, 1);
        }
        return sockfd;
    }

    int bind(int local_port, const char* local_host = "0.0.0.0") {
        if (channel == NULL || channel->isClosed()) {
            return -1;
        }
        sockaddr_u local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        int ret = sockaddr_set_ipport(&local_addr, local_host, local_port);
        if (ret != 0) {
            return NABS(ret);
        }
        ret = ::bind(channel->fd(), &local_addr.sa, SOCKADDR_LEN(&local_addr));
        if (ret != 0) {
            perror("bind");
        }
        return ret;
    }

    // closesocket thread-safe
    void closesocket() {
        if (channel) {
            channel->close(true);
        }
    }

    int startRecv() {
        if (channel == NULL || channel->isClosed()) {
            int sockfd = createsocket(remote_port, remote_host.c_str());
            if (sockfd < 0) {
                hloge("createsocket %s:%d return %d!\n", remote_host.c_str(), remote_port, sockfd);
                return sockfd;
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
        loop_->runInLoop(std::bind(&UdpClientEventLoopTmpl::startRecv, this));
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
    TSocketChannelPtr       channel;

    std::string             remote_host;
    int                     remote_port;

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
class UdpClientTmpl : private EventLoopThread, public UdpClientEventLoopTmpl<TSocketChannel> {
public:
    UdpClientTmpl(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
        , UdpClientEventLoopTmpl<TSocketChannel>(EventLoopThread::loop())
        , is_loop_owner(loop == NULL)
    {}
    virtual ~UdpClientTmpl() {
        stop(true);
    }

    const EventLoopPtr& loop() {
        return EventLoopThread::loop();
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        if (isRunning()) {
            UdpClientEventLoopTmpl<TSocketChannel>::start();
        } else {
            EventLoopThread::start(wait_threads_started, std::bind(&UdpClientTmpl::startRecv, this));
        }
    }

    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        UdpClientEventLoopTmpl<TSocketChannel>::closesocket();
        if (is_loop_owner) {
            EventLoopThread::stop(wait_threads_stopped);
        }
    }

private:
    bool is_loop_owner;
};

typedef UdpClientTmpl<SocketChannel> UdpClient;

}

#endif // HV_UDP_CLIENT_HPP_
