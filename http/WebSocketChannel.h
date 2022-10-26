#ifndef HV_WEBSOCKET_CHANNEL_H_
#define HV_WEBSOCKET_CHANNEL_H_

#include <mutex>

#include "Channel.h"

#include "wsdef.h"
#include "hmath.h"

namespace hv {

class HV_EXPORT WebSocketChannel : public SocketChannel {
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

    int send(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY, bool fin = true);

    // websocket fragment
    int send(const char* buf, int len, int fragment, enum ws_opcode opcode = WS_OPCODE_BINARY);

    int sendPing();
    int sendPong();

    int close() {
        return SocketChannel::close(type == WS_SERVER);
    }

protected:
    int sendFrame(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY, bool fin = true);

public:
    enum ws_opcode  opcode;
private:
    Buffer      sendbuf_;
    std::mutex  mutex_;
};

}

typedef std::shared_ptr<hv::WebSocketChannel> WebSocketChannelPtr;

#endif // HV_WEBSOCKET_CHANNEL_H_
