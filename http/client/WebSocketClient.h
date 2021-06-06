#ifndef HV_WEBSOCKET_CLIENT_H_
#define HV_WEBSOCKET_CLIENT_H_

/*
 * @demo examples/websocket_client_test.cpp
 */

#include "hexport.h"

#include "TcpClient.h"
#include "WebSocketChannel.h"

#include "HttpParser.h"
#include "WebSocketParser.h"

namespace hv {

class HV_EXPORT WebSocketClient : public TcpClientTmpl<WebSocketChannel> {
public:
    std::string           url;
    std::function<void()> onopen;
    std::function<void()> onclose;
    std::function<void(const std::string& msg)> onmessage;

    WebSocketClient();
    ~WebSocketClient();

    // url = ws://ip:port/path
    // url = wss://ip:port/path
    int open(const char* url);
    int close();
    int send(const std::string& msg);
    int send(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY);

    // setConnectTimeout / setPingInterval / setReconnect
    void setPingInterval(int ms) {
        ping_interval = ms;
    }

private:
    enum State {
        CONNECTING,
        CONNECTED,
        WS_UPGRADING,
        WS_OPENED,
        WS_CLOSED,
    } state;
    HttpParserPtr       http_parser_;
    HttpRequestPtr      http_req_;
    HttpResponsePtr     http_resp_;
    WebSocketParserPtr  ws_parser_;
    // ping/pong
    int                 ping_interval;
    int                 ping_cnt;
};

}

#endif // HV_WEBSOCKET_CLIENT_H_
