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

    int send(const std::string& msg, enum ws_opcode opcode = WS_OPCODE_TEXT, bool fin = true) {
        return send(msg.c_str(), msg.size(), opcode, fin);
    }

    int send(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY, bool fin = true) {
        int fragment = 0xFFFF; // 65535
        if (len > fragment) {
            return send(buf, len, fragment, opcode);
        }
        return send_(buf, len, opcode, fin);
    }

    // websocket fragment
    // send(p, fragment, opcode, false) ->
    // send(p, fragment, WS_OPCODE_CONTINUE, false) ->
    // ... ->
    // send(p, remain, WS_OPCODE_CONTINUE, true)
    int send(const char* buf, int len, int fragment, enum ws_opcode opcode = WS_OPCODE_BINARY) {
        if (len <= fragment) {
            return send_(buf, len, opcode, true);
        }

        // first fragment
        int nsend = send_(buf, fragment, opcode, false);
        if (nsend < 0) return nsend;

        const char* p = buf + fragment;
        int remain = len - fragment;
        while (remain > fragment) {
            nsend = send_(p, fragment, WS_OPCODE_CONTINUE, false);
            if (nsend < 0) return nsend;
            p += fragment;
            remain -= fragment;
        }

        // last fragment
        nsend = send_(p, remain, WS_OPCODE_CONTINUE, true);
        if (nsend < 0) return nsend;

        return len;
    }

protected:
    int send_(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY, bool fin = true) {
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
        ws_build_frame(sendbuf_.base, buf, len, mask, has_mask, opcode, fin);
        return write(sendbuf_.base, frame_size);
    }

private:
    Buffer      sendbuf_;
    std::mutex  mutex_;
};

}

typedef std::shared_ptr<hv::WebSocketChannel> WebSocketChannelPtr;

#endif // HV_WEBSOCKET_CHANNEL_H_
