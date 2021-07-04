#ifndef HV_WEBSOCKET_CHANNEL_H_
#define HV_WEBSOCKET_CHANNEL_H_

#include <mutex>

#include "Channel.h"

#include "wsdef.h"
#include "hmath.h"

namespace hv {

class WebSocketChannel : public SocketChannel {
public:
    ws_session_type type;
    WebSocketChannel(hio_t* io, ws_session_type type = WS_CLIENT)
        : SocketChannel(io)
        , type(type)
    {}
    ~WebSocketChannel() {}

    // isConnected, send, close

    int send(const std::string& msg, enum ws_opcode opcode = WS_OPCODE_TEXT) {
        return send(msg.c_str(), msg.size(), opcode);
    }

    int send(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY) {
        bool has_mask = false;
        char mask[4] = {0};
        if (type == WS_CLIENT) {
            has_mask = true;
            *(int*)mask = rand();
        }
        int frame_size = ws_calc_frame_size(len, has_mask);
        std::lock_guard<std::mutex> locker(mutex_);
        if (sendbuf_.len < frame_size) {
            sendbuf_.resize(ceil2e(frame_size));
        }
        ws_build_frame(sendbuf_.base, buf, len, mask, has_mask, opcode);
        return write(sendbuf_.base, frame_size);
    }

private:
    Buffer      sendbuf_;
    std::mutex  mutex_;
};

}

typedef std::shared_ptr<hv::WebSocketChannel> WebSocketChannelPtr;

#endif // HV_WEBSOCKET_CHANNEL_H_
