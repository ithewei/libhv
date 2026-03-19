#ifndef HV_WEBSOCKET_CHANNEL_H_
#define HV_WEBSOCKET_CHANNEL_H_

#include <mutex>

#include "Channel.h"

#include "HttpCompression.h"
#include "wsdef.h"
#include "hmath.h"

namespace hv {

class WebSocketDeflater;

class HV_EXPORT WebSocketChannel : public SocketChannel {
public:
    ws_session_type type;
    WebSocketChannel(hio_t* io, ws_session_type type = WS_CLIENT)
        : SocketChannel(io)
        , type(type)
        , opcode(WS_OPCODE_CLOSE)
    {
        compression.enabled = false;
    }
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

    void setCompression(const WebSocketCompressionOptions& options) {
        compression = options;
    }

protected:
    int sendFrame(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY, bool fin = true, bool rsv1 = false);

public:
    enum ws_opcode  opcode;
    WebSocketCompressionOptions compression;
private:
    Buffer      sendbuf_;
    std::mutex  mutex_;
    std::shared_ptr<WebSocketDeflater> deflater_;
};

}

typedef std::shared_ptr<hv::WebSocketChannel> WebSocketChannelPtr;

#endif // HV_WEBSOCKET_CHANNEL_H_
