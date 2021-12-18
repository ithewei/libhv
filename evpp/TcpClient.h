#ifndef HV_TCP_CLIENT_HPP_
#define HV_TCP_CLIENT_HPP_

#include "hsocket.h"
#include "hssl.h"
#include "hlog.h"

#include "EventLoopThread.h"
#include "Channel.h"

namespace hv {

struct ReconnectInfo {
    uint32_t min_delay;  // ms
    uint32_t max_delay;  // ms
    uint32_t cur_delay;  // ms
    /*
     * @delay_policy
     * 0: fixed
     * min_delay=3s => 3,3,3...
     * 1: linear
     * min_delay=3s max_delay=10s => 3,6,9,10,10...
     * other: exponential
     * min_delay=3s max_delay=60s delay_policy=2 => 3,6,12,24,48,60,60...
     */
    uint32_t delay_policy;
    uint32_t max_retry_cnt;
    uint32_t cur_retry_cnt;

    ReconnectInfo() {
        min_delay = 1000;
        max_delay = 60000;
        cur_delay = 0;
        // 1,2,4,8,16,32,60,60...
        delay_policy = 2;
        max_retry_cnt = INFINITE;
        cur_retry_cnt = 0;
    }
};

template<class TSocketChannel = SocketChannel>
class TcpClientTmpl {
public:
    typedef std::shared_ptr<TSocketChannel> TSocketChannelPtr;

    TcpClientTmpl() {
        tls = false;
        connect_timeout = 5000;
        enable_reconnect = false;
        enable_unpack = false;
    }

    virtual ~TcpClientTmpl() {
    }

    const EventLoopPtr& loop() {
        return loop_thread.loop();
    }

    //@retval >=0 connfd, <0 error
    int createsocket(int port, const char* host = "127.0.0.1") {
        memset(&peeraddr, 0, sizeof(peeraddr));
        int ret = sockaddr_set_ipport(&peeraddr, host, port);
        if (ret != 0) {
            return -1;
        }
        return createsocket(&peeraddr.sa);
    }
    int createsocket(struct sockaddr* peeraddr) {
        int connfd = socket(peeraddr->sa_family, SOCK_STREAM, 0);
        // SOCKADDR_PRINT(peeraddr);
        if (connfd < 0) {
            perror("socket");
            return -2;
        }

        hio_t* io = hio_get(loop_thread.hloop(), connfd);
        assert(io != NULL);
        hio_set_peeraddr(io, peeraddr, SOCKADDR_LEN(peeraddr));
        channel.reset(new TSocketChannel(io));
        return connfd;
    }
    // closesocket thread-safe
    void closesocket() {
        enable_reconnect = false;
        if (channel) {
            channel->close(true);
        }
    }

    int startConnect() {
        assert(channel != NULL);
        if (tls) {
            channel->enableSSL();
        }
        if (connect_timeout) {
            channel->setConnectTimeout(connect_timeout);
        }
        channel->onconnect = [this]() {
            if (enable_unpack) {
                channel->setUnpack(&unpack_setting);
            }
            channel->startRead();
            if (onConnection) {
                onConnection(channel);
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
            if (onConnection) {
                onConnection(channel);
            }
            // reconnect
            if (enable_reconnect) {
                startReconnect();
            } else {
                channel = NULL;
                // NOTE: channel should be destroyed,
                // so in this lambda function, no code should be added below.
            }
        };
        return channel->startConnect();
    }

    int startReconnect() {
        if (++reconnect_info.cur_retry_cnt > reconnect_info.max_retry_cnt) return 0;
        if (reconnect_info.delay_policy == 0) {
            // fixed
            reconnect_info.cur_delay = reconnect_info.min_delay;
        } else if (reconnect_info.delay_policy == 1) {
            // linear
            reconnect_info.cur_delay += reconnect_info.min_delay;
        } else {
            // exponential
            reconnect_info.cur_delay *= reconnect_info.delay_policy;
        }
        reconnect_info.cur_delay = MAX(reconnect_info.cur_delay, reconnect_info.min_delay);
        reconnect_info.cur_delay = MIN(reconnect_info.cur_delay, reconnect_info.max_delay);
        loop_thread.loop()->setTimeout(reconnect_info.cur_delay, [this](TimerID timerID){
            hlogi("reconnect... cnt=%d, delay=%d", reconnect_info.cur_retry_cnt, reconnect_info.cur_delay);
            // printf("reconnect... cnt=%d, delay=%d\n", reconnect_info.cur_retry_cnt, reconnect_info.cur_delay);
            createsocket(&peeraddr.sa);
            startConnect();
        });
        return 0;
    }

    void start(bool wait_threads_started = true) {
        loop_thread.start(wait_threads_started, std::bind(&TcpClientTmpl::startConnect, this));
    }
    // stop thread-safe
    void stop(bool wait_threads_stopped = true) {
        enable_reconnect = false;
        loop_thread.stop(wait_threads_stopped);
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

    int withTLS(const char* cert_file = NULL, const char* key_file = NULL, bool verify_peer = false) {
        if (cert_file) {
            hssl_ctx_init_param_t param;
            memset(&param, 0, sizeof(param));
            param.crt_file = cert_file;
            param.key_file = key_file;
            param.verify_peer = verify_peer ? 1 : 0;
            param.endpoint = HSSL_CLIENT;
            if (hssl_ctx_init(&param) == NULL) {
                fprintf(stderr, "hssl_ctx_init failed!\n");
                return -1;
            }
        }
        tls = true;
        return 0;
    }

    void setConnectTimeout(int ms) {
        connect_timeout = ms;
    }

    void setReconnect(ReconnectInfo* info) {
        if (info) {
            enable_reconnect = true;
            reconnect_info = *info;
        } else {
            enable_reconnect = false;
        }
    }

    void setUnpack(unpack_setting_t* setting) {
        if (setting) {
            enable_unpack = true;
            unpack_setting = *setting;
        } else {
            enable_unpack = false;
        }
    }

public:
    TSocketChannelPtr       channel;

    sockaddr_u              peeraddr;
    bool                    tls;
    int                     connect_timeout;
    bool                    enable_reconnect;
    ReconnectInfo           reconnect_info;
    bool                    enable_unpack;
    unpack_setting_t        unpack_setting;

    // Callback
    std::function<void(const TSocketChannelPtr&)>           onConnection;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onMessage;
    std::function<void(const TSocketChannelPtr&, Buffer*)>  onWriteComplete;

private:
    EventLoopThread         loop_thread;
};

typedef TcpClientTmpl<SocketChannel> TcpClient;

}

#endif // HV_TCP_CLIENT_HPP_
