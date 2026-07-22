#ifndef HV_TCP_CLIENT_HPP_
#define HV_TCP_CLIENT_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"
#include "hdns.h"

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
        dns_query = NULL;
    }

    virtual ~TcpClientEventLoopTmpl() {
        cancelReconnectTimer();
        cancelDnsQuery();
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
    // NOTE: If remote_host is a hostname (not a numeric IP), resolution is
    // deferred and done asynchronously in startConnect() so the event loop is
    // never blocked by getaddrinfo. In that case no socket/connfd exists yet
    // and this returns 0; the socket is created once the address is resolved.
    int createsocket(int remote_port, const char* remote_host = "127.0.0.1") {
        this->remote_host = remote_host;
        this->remote_port = remote_port;
        memset(&remote_addr, 0, sizeof(remote_addr));
        // numeric IP (or UDS: port < 0) -> resolve now (non-blocking) and
        // create the socket immediately, preserving the connfd contract.
        if (remote_port < 0 || is_ipaddr(remote_host)) {
            int ret = sockaddr_set_ipport(&remote_addr, remote_host, remote_port);
            if (ret != 0) {
                return NABS(ret);
            }
            return createsocket(&remote_addr.sa);
        }
        // hostname -> defer to async resolution in startConnect()
        return 0;
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
                cancelDnsQuery();
                if (channel) {
                    setReconnect(NULL);
                    channel->close();
                }
            });
        }
    }

    int startConnect() {
        loop_->assertInLoopThread();
        // If the target is a hostname, resolve it asynchronously through hdns
        // so the event loop is never blocked by getaddrinfo. This covers both
        // the first connect and every reconnect (to pick up DNS changes).
        // Numeric-IP targets skip resolution and connect directly.
        // NOTE: any TcpClientTmpl subclass (WebSocketClient, ...) gets this for
        // free; no per-client DNS glue is needed.
        if (!remote_host.empty() && !is_ipaddr(remote_host.c_str())) {
            return startResolveThenConnect();
        }
        return startConnectWithAddr();
    }

    // @internal: resolve remote_host asynchronously, then connect.
    int startResolveThenConnect() {
        cancelDnsQuery();
        hdns_options_t opt;
        opt.family = HDNS_QUERY_BOTH;
        if (connect_timeout > 0) opt.timeout_ms = connect_timeout;
        dns_query = hdns_resolve_ex(loop_->loop(), remote_host.c_str(), &opt,
                                    &TcpClientEventLoopTmpl::onDnsResolved, this);
        if (dns_query == NULL) {
            // could not start async resolve; fall back to synchronous path
            return startConnectWithAddr();
        }
        return 0;
    }

    // @internal: hdns callback (runs in loop thread).
    static void onDnsResolved(const hdns_result_t* result, void* userdata) {
        TcpClientEventLoopTmpl* self = (TcpClientEventLoopTmpl*)userdata;
        self->dns_query = NULL; // handle is invalid after the callback
        if (result->status == HDNS_STATUS_OK && result->naddrs > 0) {
            // adopt the first resolved address, keep the target port
            self->remote_addr = result->addrs[0];
            sockaddr_set_port(&self->remote_addr, self->remote_port);
        } else if (self->remote_addr.sa.sa_family == 0) {
            // resolve failed and we have no previously-resolved address to fall
            // back to. Report failure and drive reconnect (if enabled).
            hloge("resolve %s failed, status=%d", self->remote_host.c_str(), result->status);
            self->onResolveFailed();
            return;
        }
        // else: resolve failed but keep the previous remote_addr; the connect
        // attempt will fail and drive the normal reconnect path.
        self->startConnectWithAddr();
    }

    // @internal: DNS resolution failed with no usable address.
    void onResolveFailed() {
        if (onConnection && channel) {
            // channel is not connected; notify disconnect-style callback.
            onConnection(channel);
        }
        if (reconn_setting) {
            startReconnect();
        }
    }

    int startConnectWithAddr() {
        loop_->assertInLoopThread();
        if (channel == NULL || channel->isClosed()) {
            int connfd = createsocket(&remote_addr.sa);
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

    // Cancel any in-flight async DNS query (loop thread only).
    void cancelDnsQuery() {
        if (dns_query != NULL) {
            hdns_cancel(dns_query);
            dns_query = NULL;
        }
    }

private:
    EventLoopPtr    loop_;
    TimerID         reconn_timer_id;
    hdns_query_t*   dns_query;
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
