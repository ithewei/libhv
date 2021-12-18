#ifndef HV_UDP_SERVER_HPP_
#define HV_UDP_SERVER_HPP_

#include "hsocket.h"

#include "EventLoopThreadPool.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
class UdpServerTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    UdpServerTmpl() {
#if WITH_KCP
        enable_kcp = false;
#endif
    }

    virtual ~UdpServerTmpl() {
    }

    const EventLoopPtr& loop() {
        return loop_thread.loop();
    }

    //@retval >=0 bindfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        hio_t* io = hloop_create_udp_server(loop_thread.hloop(), host, port);
        if (io == NULL) return -1;
        channel.reset(new TSocketChannel(io));
        return channel->fd();
    }
    // closesocket thread-safe
    void closesocket() {
        if (channel) {
            channel->close(true);
        }
    }

    int startRecv() {
        assert(channel != NULL);
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
        if (enable_kcp) {
            hio_set_kcp(channel->io(), &kcp_setting);
        }
#endif
        return channel->startRead();
    }

    void start(bool wait_threads_started = true) {
        loop_thread.start(wait_threads_started, std::bind(&UdpServerTmpl::startRecv, this));
    }
    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        loop_thread.stop(wait_threads_stopped);
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

public:
    TSocketChannelPtr       channel;
#if WITH_KCP
    bool                    enable_kcp;
    kcp_setting_t           kcp_setting;
#endif
    // Callback
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

private:
    std::mutex              sendto_mutex;
    EventLoopThread         loop_thread;
};

typedef UdpServerTmpl<SocketChannel> UdpServer;

}

#endif // HV_UDP_SERVER_HPP_
