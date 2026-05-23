#ifndef HV_TCP_CLIENT_HPP_
#define HV_TCP_CLIENT_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"

#include "EventLoopThread.h"
#include "Channel.h"

namespace hv {

template<class TSocketChannel = SocketChannel>
// TcpClientEventLoopTmpl is a loop-bound wrapper around one outbound connection.
// When bound to an external EventLoopPtr, the caller must ensure the object is stopped and destroyed on the owner loop.
// For long-lived async usage, prefer heap allocation and use stop()/closesocket()/deleteInLoop() as the controlled teardown path.
class TcpClientEventLoopTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    TcpClientEventLoopTmpl(EventLoopPtr loop = NULL) {
        loop_ = loop ? loop : std::make_shared<EventLoop>();
        remote_port = 0;
        connect_timeout = HIO_DEFAULT_CONNECT_TIMEOUT;
        tls = false;
        tls_setting = NULL;
        reconn_setting = NULL;
        unpack_setting = NULL;
        reconn_timer_id = INVALID_TIMER_ID;
    }

    virtual ~TcpClientEventLoopTmpl() {
        cancelReconnectTimer();
        HV_FREE(tls_setting);
        HV_FREE(reconn_setting);
        HV_FREE(unpack_setting);
    }

    const EventLoopPtr& loop() {
        return loop_;
    }

    // delete thread-safe
    // NOTE: This is intended for heap objects that need to be destroyed on the owner loop.
    void deleteInLoop() {
        loop_->runInLoop([this](){
            delete this;
        });
    }

    // NOTE: By default, not bind local port. If necessary, you can call bind() after createsocket().
    // @retval >=0 connfd, <0 error
    int createsocket(int remote_port, const char* remote_host = "127.0.0.1") {
        memset(&remote_addr, 0, sizeof(remote_addr));
        int ret = sockaddr_set_ipport(&remote_addr, remote_host, remote_port);
        if (ret != 0) {
            return NABS(ret);
        }
        this->remote_host = remote_host;
        this->remote_port = remote_port;
        return createsocket(&remote_addr.sa);
    }

    int createsocket(struct sockaddr* remote_addr) {
        int connfd = ::socket(remote_addr->sa_family, SOCK_STREAM, 0);
        // SOCKADDR_PRINT(remote_addr);
        if (connfd < 0) {
            perror("socket");
            return -2;
        }

        hio_t* io = hio_get(loop_->loop(), connfd);
        assert(io != NULL);
        hio_set_peeraddr(io, remote_addr, SOCKADDR_LEN(remote_addr));
        channel = std::make_shared<TSocketChannel>(io);
        return connfd;
    }

    int bind(int local_port, const char* local_host = "0.0.0.0") {
        sockaddr_u local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        int ret = sockaddr_set_ipport(&local_addr, local_host, local_port);
        if (ret != 0) {
            return NABS(ret);
        }
        return bind(&local_addr.sa);
    }

    int bind(struct sockaddr* local_addr) {
        if (channel == NULL || channel->isClosed()) {
            return -1;
        }
        int ret = ::bind(channel->fd(), local_addr, SOCKADDR_LEN(local_addr));
        if (ret != 0) {
            perror("bind");
        }
        return ret;
    }

    // closesocket thread-safe
    void closesocket() {
        if (channel && channel->status != SocketChannel::CLOSED) {
            loop_->runInLoop([this](){
                if (channel) {
                    setReconnect(NULL);
                    channel->close();
                }
            });
        }
    }

    int startConnect() {
        loop_->assertInLoopThread();
        if (channel == NULL || channel->isClosed()) {
            int connfd = -1;
            if (reconn_setting && reconn_setting->cur_retry_cnt > 1) {
                // Resolve DNS to get the latest IP address
                connfd = createsocket(remote_port, remote_host.c_str());
            } else {
                connfd = createsocket(&remote_addr.sa);
            }
            if (connfd < 0) {
                hloge("createsocket %s:%d return %d!\n", remote_host.c_str(), remote_port, connfd);
                return connfd;
            }
        }
        if (channel == NULL || channel->status >= SocketChannel::CONNECTING) {
            return -1;
        }
        if (connect_timeout) {
            channel->setConnectTimeout(connect_timeout);
        }
        if (tls) {
            channel->enableSSL();
            if (tls_setting) {
                int ret = channel->newSslCtx(tls_setting);
                if (ret != 0) {
                    hloge("new SSL_CTX failed: %d", ret);
                    closesocket();
                    return ret;
                }
            }
            if (!is_ipaddr(remote_host.c_str())) {
                channel->setHostname(remote_host);
            }
        }
        channel->onconnect = [this]() {
            if (unpack_setting) {
                channel->setUnpack(unpack_setting);
            }
            channel->startRead();
            if (onConnection) {
                onConnection(channel);
            }
            if (reconn_setting) {
                reconn_setting_reset(reconn_setting);
            }
        };
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
        channel->onclose = [this]() {
            bool reconnect = reconn_setting != NULL;
            if (onConnection) {
                onConnection(channel);
            }
            if (reconnect) {
                startReconnect();
            }
        };
        return channel->startConnect();
    }

    int startReconnect() {
        loop_->assertInLoopThread();
        if (!reconn_setting) return -1;
        if (!reconn_setting_can_retry(reconn_setting)) return -2;
        uint32_t delay = reconn_setting_calc_delay(reconn_setting);
        hlogi("reconnect... cnt=%d, delay=%d", reconn_setting->cur_retry_cnt, reconn_setting->cur_delay);
        reconn_timer_id = loop_->setTimeout(delay, [this](TimerID timerID){
            if (reconn_timer_id == timerID) {
                reconn_timer_id = INVALID_TIMER_ID;
            }
            startConnect();
        });
        return 0;
    }

    // start thread-safe
    void start() {
        loop_->runInLoop(std::bind(&TcpClientEventLoopTmpl::startConnect, this));
    }

    bool isConnected() {
        if (channel == NULL) return false;
        return channel->isConnected();
    }

    // send thread-safe
    int send(const void* data, int size) {
        if (!isConnected()) return -1;
        return channel->write(data, size);
    }
    int send(Buffer* buf) {
        return send(buf->data(), buf->size());
    }
    int send(const std::string& str) {
        return send(str.data(), str.size());
    }

    int withTLS(hssl_ctx_opt_t* opt = NULL) {
        tls = true;
        if (opt) {
            if (tls_setting == NULL) {
                HV_ALLOC_SIZEOF(tls_setting);
            }
            opt->endpoint = HSSL_CLIENT;
            *tls_setting = *opt;
        }
        return 0;
    }

    void setConnectTimeout(int ms) {
        connect_timeout = ms;
    }

    void setReconnect(reconn_setting_t* setting) {
        if (setting == NULL) {
            cancelReconnectTimer();
            HV_FREE(reconn_setting);
            return;
        }
        if (reconn_setting == NULL) {
            HV_ALLOC_SIZEOF(reconn_setting);
        }
        *reconn_setting = *setting;
    }
    bool isReconnect() {
        return reconn_setting && reconn_setting->cur_retry_cnt > 0;
    }

    void setUnpack(unpack_setting_t* setting) {
        if (setting == NULL) {
            HV_FREE(unpack_setting);
            return;
        }
        if (unpack_setting == NULL) {
            HV_ALLOC_SIZEOF(unpack_setting);
        }
        *unpack_setting = *setting;
    }

public:
    TSocketChannelPtr       channel;

    std::string             remote_host;
    int                     remote_port;
    sockaddr_u              remote_addr;
    int                     connect_timeout;
    bool                    tls;
    hssl_ctx_opt_t*         tls_setting;
    reconn_setting_t*       reconn_setting;
    unpack_setting_t*       unpack_setting;

    // Callback
    std::function<void(const TSocketChannelPtr&)>           onConnection;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    // NOTE: Use Channel::isWriteComplete in onWriteComplete callback to determine whether all data has been written.
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

private:
    void cancelReconnectTimer() {
        if (reconn_timer_id != INVALID_TIMER_ID) {
            loop_->killTimer(reconn_timer_id);
            reconn_timer_id = INVALID_TIMER_ID;
        }
    }

private:
    EventLoopPtr    loop_;
    TimerID         reconn_timer_id;
};

template<class TSocketChannel = SocketChannel>
class TcpClientTmpl : private EventLoopThread, public TcpClientEventLoopTmpl<TSocketChannel> {
public:
    TcpClientTmpl(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
        , TcpClientEventLoopTmpl<TSocketChannel>(EventLoopThread::loop())
        , is_loop_owner(loop == NULL)
    {}
    virtual ~TcpClientTmpl() {
        stop(true);
    }

    const EventLoopPtr& loop() {
        return EventLoopThread::loop();
    }

    // start thread-safe
    void start(bool wait_threads_started = true) {
        if (isRunning()) {
            TcpClientEventLoopTmpl<TSocketChannel>::start();
        } else {
            EventLoopThread::start(wait_threads_started, [this]() {
                TcpClientTmpl::startConnect();
                return 0;
            });
        }
    }

    // stop thread-safe
    // NOTE: When constructed with an external loop, this only closes the socket and does not stop that loop.
    void stop(bool wait_threads_stopped = true) {
        TcpClientEventLoopTmpl<TSocketChannel>::closesocket();
        if (is_loop_owner) {
            EventLoopThread::stop(wait_threads_stopped);
        }
    }

private:
    bool is_loop_owner;
};

typedef TcpClientTmpl<SocketChannel> TcpClient;

}

#endif // HV_TCP_CLIENT_HPP_
