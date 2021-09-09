#ifndef HV_CHANNEL_HPP_
#define HV_CHANNEL_HPP_

#include <string>
#include <functional>
#include <memory>

#include "hloop.h"
#include "hsocket.h"

#include "Buffer.h"

namespace hv {

class Channel {
public:
    Channel(hio_t* io = NULL) {
        io_ = io;
        fd_ = -1;
        id_ = 0;
        ctx_ = NULL;
        if (io) {
            fd_ = hio_fd(io);
            id_ = hio_id(io);
            ctx_ = hio_context(io);
            hio_set_context(io, this);
            if (hio_getcb_read(io) == NULL) {
                hio_setcb_read(io_, on_read);
            }
            if (hio_getcb_write(io) == NULL) {
                hio_setcb_write(io_, on_write);
            }
            if (hio_getcb_close(io) == NULL) {
                hio_setcb_close(io_, on_close);
            }
        }
        status = isOpened() ? OPENED : CLOSED;
    }

    virtual ~Channel() {
        close();
    }

    hio_t*      io() { return io_; }
    int         fd() { return fd_; }
    uint32_t    id() { return id_; }
    int error() { return hio_error(io_); }

    // context
    void* context() {
        return ctx_;
    }
    void setContext(void* ctx) {
        ctx_ = ctx;
    }
    template<class T>
    T* newContext() {
        ctx_ = new T;
        return (T*)ctx_;
    }
    template<class T>
    T* getContext() {
        return (T*)ctx_;
    }
    template<class T>
    void deleteContext() {
        if (ctx_) {
            delete (T*)ctx_;
            ctx_ = NULL;
        }
    }

    bool isOpened() {
        if (io_ == NULL) return false;
        return id_ == hio_id(io_) && hio_is_opened(io_);
    }
    bool isClosed() {
        return !isOpened();
    }

    int startRead() {
        if (!isOpened()) return -1;
        return hio_read_start(io_);
    }

    int stopRead() {
        if (!isOpened()) return -1;
        return hio_read_stop(io_);
    }

    int write(const void* data, int size) {
        if (!isOpened()) return -1;
        return hio_write(io_, data, size);
    }

    int write(Buffer* buf) {
        return write(buf->data(), buf->size());
    }

    int write(const std::string& str) {
        return write(str.data(), str.size());
    }

    int close(bool async = false) {
        if (!isOpened()) return -1;
        if (async) {
            return hio_close_async(io_);
        }
        return hio_close(io_);
    }

public:
    hio_t*      io_;
    int         fd_;
    uint32_t    id_;
    void*       ctx_;
    enum Status {
        // Channel::Status
        OPENED,
        CLOSED,
        // SocketChannel::Status
        CONNECTING,
        CONNECTED,
        DISCONNECTED,
    } status;
    std::function<void(Buffer*)> onread;
    std::function<void(Buffer*)> onwrite;
    std::function<void()>        onclose;

private:
    static void on_read(hio_t* io, void* data, int readbytes) {
        Channel* channel = (Channel*)hio_context(io);
        if (channel && channel->onread) {
            Buffer buf(data, readbytes);
            channel->onread(&buf);
        }
    }

    static void on_write(hio_t* io, const void* data, int writebytes) {
        Channel* channel = (Channel*)hio_context(io);
        if (channel && channel->onwrite) {
            Buffer buf((void*)data, writebytes);
            channel->onwrite(&buf);
        }
    }

    static void on_close(hio_t* io) {
        Channel* channel = (Channel*)hio_context(io);
        if (channel) {
            channel->status = CLOSED;
            if (channel->onclose) {
                channel->onclose();
            }
        }
    }
};

class SocketChannel : public Channel {
public:
    // for TcpClient
    std::function<void()>   onconnect;
    std::function<void()>   heartbeat;

    SocketChannel(hio_t* io) : Channel(io) {
    }
    virtual ~SocketChannel() {}

    int enableSSL() {
        return hio_enable_ssl(io_);
    }

    void setConnectTimeout(int timeout_ms) {
        if (io_ == NULL) return;
        hio_set_connect_timeout(io_, timeout_ms);
    }

    void setCloseTimeout(int timeout_ms) {
        if (io_ == NULL) return;
        hio_set_close_timeout(io_, timeout_ms);
    }

    void setKeepaliveTimeout(int timeout_ms) {
        if (io_ == NULL) return;
        hio_set_keepalive_timeout(io_, timeout_ms);
    }

    void setHeartbeat(int interval_ms, std::function<void()> fn) {
        if (io_ == NULL) return;
        heartbeat = std::move(fn);
        hio_set_heartbeat(io_, interval_ms, send_heartbeat);
    }

    void setUnpack(unpack_setting_t* setting) {
        if (io_ == NULL) return;
        hio_set_unpack(io_, setting);
    }

    int startConnect(int port, const char* host = "127.0.0.1") {
        sockaddr_u peeraddr;
        memset(&peeraddr, 0, sizeof(peeraddr));
        int ret = sockaddr_set_ipport(&peeraddr, host, port);
        if (ret != 0) {
            // hloge("unknown host %s", host);
            return ret;
        }
        return startConnect(&peeraddr.sa);
    }

    int startConnect(struct sockaddr* peeraddr) {
        if (io_ == NULL) return -1;
        hio_set_peeraddr(io_, peeraddr, SOCKADDR_LEN(peeraddr));
        return startConnect();
    }

    int startConnect() {
        if (io_ == NULL) return -1;
        status = CONNECTING;
        hio_setcb_connect(io_, on_connect);
        return hio_connect(io_);
    }

    bool isConnected() {
        return isOpened() && status == CONNECTED;
    }

    std::string localaddr() {
        if (io_ == NULL) return "";
        struct sockaddr* addr = hio_localaddr(io_);
        char buf[SOCKADDR_STRLEN] = {0};
        return SOCKADDR_STR(addr, buf);
    }

    std::string peeraddr() {
        if (io_ == NULL) return "";
        struct sockaddr* addr = hio_peeraddr(io_);
        char buf[SOCKADDR_STRLEN] = {0};
        return SOCKADDR_STR(addr, buf);
    }

private:
    static void on_connect(hio_t* io) {
        SocketChannel* channel = (SocketChannel*)hio_context(io);
        if (channel) {
            channel->status = CONNECTED;
            if (channel->onconnect) {
                channel->onconnect();
            }
        }
    }

    static void send_heartbeat(hio_t* io) {
        SocketChannel* channel = (SocketChannel*)hio_context(io);
        if (channel && channel->heartbeat) {
            channel->heartbeat();
        }
    }
};

typedef std::shared_ptr<Channel>        ChannelPtr;
typedef std::shared_ptr<SocketChannel>  SocketChannelPtr;

}

#endif // HV_CHANNEL_HPP_
