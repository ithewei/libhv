#ifndef HV_WEBSOCKET_CHANNEL_H_
#define HV_WEBSOCKET_CHANNEL_H_

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

    int send(const std::string& msg) {
        bool has_mask = false;
        char mask[4] = {0};
        if (type == WS_CLIENT) {
            has_mask = true;
            *(int*)mask = rand();
        }
        int frame_size = ws_calc_frame_size(msg.size(), has_mask);
        if (sendbuf.len < frame_size) {
            sendbuf.resize(ceil2e(frame_size));
        }
        ws_build_frame(sendbuf.base, msg.c_str(), msg.size(), mask, has_mask);
        Buffer buf(sendbuf.base, frame_size);
        return write(&buf);
    }

private:
    Buffer sendbuf;
};

}

typedef std::shared_ptr<hv::WebSocketChannel> WebSocketChannelPtr;

#endif // HV_WEBSOCKET_CHANNEL_H_
