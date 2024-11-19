#ifndef HV_UDP_GROUP_HPP_
#define HV_UDP_GROUP_HPP_
#include "hsocket.h"

#include "EventLoopThreadPool.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
class UdpGroupEventLoopTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    UdpGroupEventLoopTmpl(EventLoopPtr loop = NULL) {
        loop_ = loop ? loop : std::make_shared<EventLoop>();
        port = 0;
#if WITH_KCP
        kcp_setting = NULL;
#endif
    }

    virtual ~UdpGroupEventLoopTmpl() {
#if WITH_KCP
        HV_FREE(kcp_setting);
#endif
    }

    const EventLoopPtr& loop() {
        return loop_;
    }

    //use createsocket() for server OR use createsocketRemote() for client
	//@retval >=0 bindfd, <0 error
    int createsocket(int port, const char* host = "0.0.0.0") {
        hio_t* io = hloop_create_udp_server(loop_->loop(), host, port);
        if (io == NULL) return -1;
        this->host = host;
        this->port = port;
        channel = std::make_shared<TSocketChannel>(io);
        return channel->fd();
    }

	// join group
	int joinGroup(const char* g){
		if(channel == NULL || channel->isClosed()){
			return -1;
		}
		return udp_joingroupv4(channel->fd(), g, host.c_str());
    }
	// leave group
	int leaveGroup(const char* g){
		if(channel == NULL || channel->isClosed()){
			return -1;
		}
		return udp_leavegroupv4(channel->fd(), g, host.c_str());
    }

    // closesocket thread-safe
    void closesocket() {
        if (channel) {
            channel->close(true);
        }
    }

    int startRecv() {
        if (channel == NULL || channel->isClosed()) {
            return -1;
        }
        channel->onread = [this](Buffer* buf) {
            if (onMessage) {
                onMessage(channel, buf);
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
        loop_->runInLoop(std::bind(&UdpGroupEventLoopTmpl::startRecv, this));
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
private:
    EventLoopPtr            loop_;
};

template<class TSocketChannel = SocketChannel>
class UdpGroupTmpl : private EventLoopThread, public UdpGroupEventLoopTmpl<TSocketChannel> {
public:
    UdpGroupTmpl(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
        , UdpGroupEventLoopTmpl<TSocketChannel>(EventLoopThread::loop())
        , is_loop_owner(loop == NULL)
    {}
    virtual ~UdpGroupTmpl() {
        stop(true);
    }

    const EventLoopPtr& loop() {
        return EventLoopThread::loop();
    }

	// join group
	int joinGroup(const char* g){
		return UdpGroupEventLoopTmpl<TSocketChannel>::joinGroup(g);
	}

    // leave group
	int leaveGroup(const char* g){
		return UdpGroupEventLoopTmpl<TSocketChannel>::leaveGroup(g);
	}

    // start thread-safe
    void start(bool wait_threads_started = true) {
        if (isRunning()) {
            UdpGroupEventLoopTmpl<TSocketChannel>::start();
        } else {
            EventLoopThread::start(wait_threads_started, std::bind(&UdpGroupTmpl::startRecv, this));
        }
    }

    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        UdpGroupEventLoopTmpl<TSocketChannel>::closesocket();
        if (is_loop_owner) {
            EventLoopThread::stop(wait_threads_stopped);
        }
    }

private:
    bool is_loop_owner;
};

typedef UdpGroupTmpl<SocketChannel> UdpGroup;

}

#endif // HV_UDP_GROUP_HPP_
