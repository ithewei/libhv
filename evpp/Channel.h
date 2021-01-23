#ifndef HV_CHANNEL_HPP_
#define HV_CHANNEL_HPP_

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
            hio_set_context(io, this);
            hio_setcb_read(io_, on_read);
            hio_setcb_write(io_, on_write);
            hio_setcb_close(io_, on_close);
        }
    }

    virtual ~Channel() {
        close();
    }

    hio_t* io() { return io_; }
    int fd() { return fd_; }
    int id() { return id_; }
    int error() { return hio_error(io_); }

    void setContext(void* ctx) {
        ctx_ = ctx;
    }
    void* context() {
        return ctx_;
    }

    bool isOpened() {
        if (io_ == NULL) return false;
        return id_ == hio_id(io_) && hio_is_opened(io_);
    }
    bool isClosed() {
        return !isOpened();
    }

    int startRead() {
        if (!isOpened()) return 0;
        return hio_read_start(io_);
    }

    int stopRead() {
        if (!isOpened()) return 0;
        return hio_read_stop(io_);
    }

    int write(Buffer* buf) {
        if (!isOpened()) return 0;
        return hio_write(io_, buf->data(), buf->size());
    }

    int write(const std::string& str) {
        if (!isOpened()) return 0;
        return hio_write(io_, str.data(), str.size());
    }

    int close() {
        if (!isOpened()) return 0;
        return hio_close(io_);
    }

public:
    hio_t*      io_;
    int         fd_;
    uint32_t    id_;
    void*       ctx_;
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
        if (channel && channel->onclose) {
            channel->onclose();
        }
    }
};

class SocketChannel : public Channel {
public:
    enum Status {
        OPENED,
        CONNECTING,
        CONNECTED,
        DISCONNECTED,
        CLOSED,
    } status;

    SocketChannel(hio_t* io) : Channel(io) {
        status = isOpened() ? OPENED : CLOSED;
    }
    virtual ~SocketChannel() {}

    bool isConnected() {
        return isOpened() && status == CONNECTED;
    }

    std::string localaddr() {
        struct sockaddr* addr = hio_localaddr(io_);
        char buf[SOCKADDR_STRLEN] = {0};
        return SOCKADDR_STR(addr, buf);
    }

    std::string peeraddr() {
        struct sockaddr* addr = hio_peeraddr(io_);
        char buf[SOCKADDR_STRLEN] = {0};
        return SOCKADDR_STR(addr, buf);
    }

    int send(const std::string& str) {
        return write(str);
    }
};

typedef std::shared_ptr<Channel>        ChannelPtr;
typedef std::shared_ptr<SocketChannel>  SocketChannelPtr;

}

#endif // HV_CHANNEL_HPP_
