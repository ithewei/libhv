#ifndef HV_TCP_CLIENT_HPP_
#define HV_TCP_CLIENT_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"
#include "herr.h"
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
        dns_id = INVALID_DNS_ID;
        reconn_success_cnt_ = 0;
        reconn_retry_cnt_ = 0;
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
    //
    // @retval >0  connfd: the socket was created immediately (remote_host is a
    //             numeric IP, or a Unix Domain Socket when remote_port < 0).
    // @retval =0  pending: remote_host is a hostname, so socket creation is
    //             deferred until the address is resolved asynchronously in
    //             startConnect() (avoids blocking the event loop on getaddrinfo).
    //             This is NOT an error; the real fd is available later via
    //             channel->fd(). Callers should treat >=0 as success and only
    //             <0 as failure.
    // @retval <0  error.
    //
    // Rationale for the =0 case: a socket's address family (AF_INET vs AF_INET6)
    // must be known before socket() is called, but for a hostname the family is
    // only known after DNS returns an A (IPv4) or AAAA (IPv6) record. So the
    // socket cannot be created up front and is created once resolution completes
    // in startConnectWithAddr() -- the same order as the standard getaddrinfo
    // connect loop.
    int createsocket(int remote_port, const char* remote_host = "127.0.0.1") {
        this->remote_host = remote_host;
        this->remote_port = remote_port;
        memset(&remote_addr, 0, sizeof(remote_addr));
        // numeric IP (or UDS: port < 0) -> resolve now (non-blocking) and
        // create the socket immediately, returning the connfd (>0).
        if (remote_port < 0 || is_ipaddr(remote_host)) {
            int ret = sockaddr_set_ipport(&remote_addr, remote_host, remote_port);
            if (ret != 0) {
                return NABS(ret);
            }
            return createsocket(&remote_addr.sa);
        }
        // hostname -> defer socket creation to async resolution in
        // startConnect(); return 0 (pending), not an error.
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
        // NOTE: Unix Domain Socket targets (remote_port < 0) carry a filesystem
        // path in remote_host, not a hostname; remote_addr is already set by
        // createsocket(), so never run DNS on them.
        if (remote_port >= 0 && !remote_host.empty() && !is_ipaddr(remote_host.c_str())) {
            return startResolveThenConnect();
        }
        return startConnectWithAddr();
    }

    // @internal: resolve remote_host asynchronously, then connect.
    // Uses EventLoop::resolveDns which returns a use-after-free-proof DnsID and
    // manages the underlying hdns_t lifetime, so this class only holds an id.
    int startResolveThenConnect() {
        cancelDnsQuery();
        hdns_setting_t opt;
        opt.family = HDNS_QUERY_BOTH;
        if (connect_timeout > 0) opt.timeout_ms = connect_timeout;
        dns_id = loop_->resolveDns(remote_host.c_str(),
            [this](int status, int naddrs, const sockaddr_u* addrs) {
                dns_id = INVALID_DNS_ID; // this query is done
                if (status == HDNS_STATUS_OK && naddrs > 0) {
                    // adopt the first resolved address, keep the target port
                    remote_addr = addrs[0];
                    sockaddr_set_port(&remote_addr, remote_port);
                } else if (remote_addr.sa.sa_family == 0) {
                    // resolve failed and no previously-resolved address to fall
                    // back to. Report failure and drive reconnect (if enabled).
                    hloge("resolve %s failed, status=%d", remote_host.c_str(), status);
                    onDnsResolveFailed();
                    return;
                }
                // else: resolve failed but keep the previous remote_addr; the
                // connect attempt will fail and drive the normal reconnect path.
                startConnectWithAddr();
            }, &opt);
        if (dns_id == INVALID_DNS_ID) {
            // Could not start async resolve. If we already have a usable address
            // (a previous resolve), fall back to a direct connect; otherwise
            // (hostname first attempt, remote_addr unset) report a DNS failure
            // rather than calling socket(0, ...) with an unset family.
            if (remote_addr.sa.sa_family == 0) {
                onDnsResolveFailed();
                return 0;
            }
            return startConnectWithAddr();
        }
        return 0;
    }

    // @internal: DNS resolution failed with no usable address.
    // Ensure the user is always notified (even on the very first attempt, when
    // no channel exists yet). onConnection users expect a valid (non-null)
    // channel, so create a NULL-io channel: it reports isConnected()==false and
    // all its methods guard against a null io, matching the connect-failure
    // contract without fabricating a real socket.
    void onDnsResolveFailed() {
        if (channel == NULL) {
            channel = std::make_shared<TSocketChannel>((hio_t*)NULL);
        }
        // record the reason so the user can distinguish it via channel->error()
        // (there is no io_ to carry the error on the first-attempt path).
        channel->setError(ERR_DNS_RESOLVE);
        notifyDisconnectThenReconnect();
    }

    // @internal: notify a disconnect via onConnection, then reconnect if set.
    // Snapshots the reconnect flag before the callback so a user that disables
    // reconnect (setReconnect(NULL)) inside onConnection still won't reconnect.
    // CONTRACT: when reconnect is enabled, the user must NOT destroy this client
    // from within onConnection -- startReconnect() runs after the callback and
    // is a member call, so destroying here would be a use-after-free. To tear
    // down from onConnection, call setReconnect(NULL) first (or defer the
    // destroy, e.g. via deleteInLoop()). This matches the long-standing evpp
    // onclose behavior; shared by the onclose path and the DNS-failure path.
    void notifyDisconnectThenReconnect() {
        bool reconnect = reconn_setting != NULL;
        if (onConnection) {
            onConnection(channel);
        }
        if (reconnect) {
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
            // reconn_retry_cnt_ > 0 means this connection was driven by
            // auto-reconnect (set in startReconnect when the attempt was
            // scheduled). Count the success here; do NOT touch reconn_retry_cnt_
            // so isReconnect()/reconnectRetries() stay valid for the whole
            // connection lifetime, including WebSocketClient::onopen.
            if (reconn_retry_cnt_ > 0) {
                ++reconn_success_cnt_;
            }
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
            notifyDisconnectThenReconnect();
        };
        return channel->startConnect();
    }

    int startReconnect() {
        loop_->assertInLoopThread();
        if (!reconn_setting) return -1;
        if (!reconn_setting_can_retry(reconn_setting)) return -2;
        // A reconnect attempt is being scheduled: record it now (not only on
        // success) so isReconnect()/reconnectRetries() are correct even while
        // the reconnect loop is still failing (e.g. read in onclose).
        reconn_retry_cnt_ = reconn_setting->cur_retry_cnt;
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
            // Disabling auto-reconnect ends the reconnect lifecycle, so clear
            // the stats. This also covers closesocket(), which calls this.
            // (Reconfiguring with a non-NULL setting keeps the counts, since
            // that is usually just tuning delay/retry, not a fresh start.)
            reconn_success_cnt_ = 0;
            reconn_retry_cnt_ = 0;
            return;
        }
        if (reconn_setting == NULL) {
            HV_ALLOC_SIZEOF(reconn_setting);
        }
        *reconn_setting = *setting;
    }
    // Reconnect stats. Updated when an auto-reconnect attempt is scheduled (not
    // only when it succeeds), so they are correct in every callback -- including
    // onclose during a still-failing reconnect loop, and callbacks that fire
    // later than onConnection (e.g. WebSocketClient::onopen, after the HTTP
    // upgrade). They reset to 0 when auto-reconnect is disabled via
    // setReconnect(NULL) (which closesocket() also does) or on a fresh start().
    //
    // isReconnect():           whether a reconnect cycle has been entered, i.e.
    //                          the current/last connection attempt was driven by
    //                          auto-reconnect (true in onclose while retrying and
    //                          in onopen after a reconnect succeeds).
    // reconnectRetries():      attempts made in the current reconnect cycle
    //                          (1 on the first retry, 2 on the next...); mirrors
    //                          reconn_setting->cur_retry_cnt but survives the
    //                          reset that happens once a connection succeeds.
    // reconnectSuccessCount(): how many times auto-reconnect has succeeded so
    //                          far (1 on the first reconnect, 2 on the next...).
    bool isReconnect() {
        return reconn_retry_cnt_ > 0;
    }
    uint32_t reconnectRetries() {
        return reconn_retry_cnt_;
    }
    uint32_t reconnectSuccessCount() {
        return reconn_success_cnt_;
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
        if (dns_id != INVALID_DNS_ID) {
            loop_->cancelDns(dns_id);
            dns_id = INVALID_DNS_ID;
        }
    }

    EventLoopPtr    loop_;
    TimerID         reconn_timer_id;
    DnsID           dns_id;
    uint32_t        reconn_success_cnt_;   // # of successful auto-reconnects (latched)
    uint32_t        reconn_retry_cnt_;     // attempts in the current reconnect cycle
};

template<class TSocketChannel = SocketChannel>
class TcpClientTmpl : private EventLoopThread, public TcpClientEventLoopTmpl<TSocketChannel> {
public:
    TcpClientTmpl(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
        , TcpClientEventLoopTmpl<TSocketChannel>(EventLoopThread::loop())
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
    // NOTE: When constructed with an external loop, this only closes the socket;
    // EventLoopThread::stop() will not stop a loop this client does not own.
    void stop(bool wait_threads_stopped = true) {
        TcpClientEventLoopTmpl<TSocketChannel>::closesocket();
        EventLoopThread::stop(wait_threads_stopped);
    }
};

typedef TcpClientTmpl<SocketChannel> TcpClient;

}

#endif // HV_TCP_CLIENT_HPP_
