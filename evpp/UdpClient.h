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

    void start(bool wait_threads_started = true) {
        loop_thread.start(wait_threads_started,
            [this]() {
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
                channel->startRead();
                return 0;
            }
        );
    }
    void stop(bool wait_threads_stopped = true) {
        loop_thread.stop(wait_threads_stopped);
    }

    int sendto(const void* data, int size) {
        if (channel == NULL) return 0;
        return channel->write(data, size);
    }

    int sendto(Buffer* buf) {
        if (channel == NULL) return 0;
        return channel->write(buf);
    }

    int sendto(const std::string& str) {
        if (channel == NULL) return 0;
        return channel->write(str);
    }

public:
    SocketChannelPtr        channel;
    // Callback
    MessageCallback         onMessage;
    WriteCompleteCallback   onWriteComplete;
private:
    EventLoopThread         loop_thread;
};

}

#endif // HV_UDP_CLIENT_HPP_
