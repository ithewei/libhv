#ifndef HV_UDP_CLIENT_HPP_
#define HV_UDP_CLIENT_HPP_

#include "hsocket.h"

#include "EventLoopThread.h"
#include "Callback.h"
#include "Channel.h"

namespace hv {

class UdpClient {
public:
    UdpClient() {
#if WITH_KCP
        enable_kcp = false;
#endif
    }

    virtual ~UdpClient() {
    }

    const EventLoopPtr& loop() {
        return loop_thread.loop();
    }

    //@retval >=0 sockfd, <0 error
    int createsocket(int port, const char* host = "127.0.0.1") {
        hio_t* io = hloop_create_udp_client(loop_thread.hloop(), host, port);
        if (io == NULL) return -1;
        channel.reset(new SocketChannel(io));
        return channel->fd();
    }
    void closesocket() {
        if (channel) {
            channel->close();
            channel = NULL;
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
        loop_thread.start(wait_threads_started, std::bind(&UdpClient::startRecv, this));
    }
    void stop(bool wait_threads_stopped = true) {
        loop_thread.stop(wait_threads_stopped);
    }

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
        if (setting) {
            enable_kcp = true;
            kcp_setting = *setting;
        } else {
            enable_kcp = false;
        }
    }
#endif

public:
    SocketChannelPtr        channel;
#if WITH_KCP
    bool                    enable_kcp;
    kcp_setting_t           kcp_setting;
#endif
    // Callback
    MessageCallback         onMessage;
    WriteCompleteCallback   onWriteComplete;

private:
    std::mutex              sendto_mutex;
    EventLoopThread         loop_thread;
};

}

#endif // HV_UDP_CLIENT_HPP_
