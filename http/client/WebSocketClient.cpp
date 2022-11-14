#include "WebSocketClient.h"

#include "base64.h"
#include "hlog.h"

#define DEFAULT_WS_PING_INTERVAL    3000 // ms

namespace hv {

WebSocketClient::WebSocketClient(EventLoopPtr loop)
    : TcpClientTmpl<WebSocketChannel>(loop)
{
    state = WS_CLOSED;
    ping_interval = DEFAULT_WS_PING_INTERVAL;
    ping_cnt = 0;
}

WebSocketClient::~WebSocketClient() {
    stop();
}

/*
 * ParseUrl => createsocket => start =>
 * TCP::onConnection => websocket_handshake => WS::onopen =>
 * TCP::onMessage => WebSocketParser => WS::onmessage =>
 * TCP::onConnection => WS::onclose
 */
int WebSocketClient::open(const char* _url, const http_headers& headers) {
    close();

    // ParseUrl
    if (_url) {
        if (strncmp(_url, "ws", 2) != 0) {
            url = "ws://";
            url += _url;
        } else {
            url = _url;
        }
    }
    hlogi("%s", url.c_str());
    if (!http_req_) {
        http_req_.reset(new HttpRequest);
    }
    // ws => http
    http_req_->url = "http" + url.substr(2, -1);
    http_req_->ParseUrl();

    int connfd = createsocket(http_req_->port, http_req_->host.c_str());
    if (connfd < 0) {
        hloge("createsocket %s:%d return %d!", http_req_->host.c_str(), http_req_->port, connfd);
        return connfd;
    }

    // wss
    bool wss = strncmp(url.c_str(), "wss", 3) == 0;
    if (wss) {
        withTLS();
    }

    for (auto& header : headers) {
        http_req_->headers[header.first] = header.second;
    }

    onConnection = [this](const WebSocketChannelPtr& channel) {
        if (channel->isConnected()) {
            state = CONNECTED;
            // websocket_handshake
            http_req_->headers["Connection"] = "Upgrade";
            http_req_->headers["Upgrade"] = "websocket";
            if (http_req_->GetHeader(SEC_WEBSOCKET_KEY).empty()) {
                // generate SEC_WEBSOCKET_KEY
                unsigned char rand_key[16] = {0};
                int *p = (int*)rand_key;
                for (int i = 0; i < 4; ++i, ++p) {
                    *p = rand();
                }
                char ws_key[32] = {0};
                hv_base64_encode(rand_key, 16, ws_key);
                http_req_->headers[SEC_WEBSOCKET_KEY] = ws_key;
            }
            if (http_req_->GetHeader(SEC_WEBSOCKET_VERSION).empty()) {
                http_req_->headers[SEC_WEBSOCKET_VERSION] = "13";
            }
            std::string http_msg = http_req_->Dump(true, true);
            // printf("%s", http_msg.c_str());
            // NOTE: not use WebSocketChannel::send
            channel->write(http_msg);
            state = WS_UPGRADING;
            // prepare HttpParser
            http_parser_.reset(HttpParser::New(HTTP_CLIENT, HTTP_V1));
            http_resp_.reset(new HttpResponse);
            http_parser_->InitResponse(http_resp_.get());
        } else {
            state = WS_CLOSED;
            if (onclose) onclose();
        }
    };
    onMessage = [this](const WebSocketChannelPtr& channel, Buffer* buf) {
        const char* data = (const char*)buf->data();
        size_t size = buf->size();
        if (state == WS_UPGRADING) {
            int nparse = http_parser_->FeedRecvData(data, size);
            if (nparse != size && http_parser_->GetError()) {
                hloge("http parse error!");
                channel->close();
                return;
            }
            data += nparse;
            size -= nparse;
            if (http_parser_->IsComplete()) {
                if (http_resp_->status_code != HTTP_STATUS_SWITCHING_PROTOCOLS) {
                    // printf("websocket response:\n%s\n", http_resp_->Dump(true, true).c_str());
                    if (http_req_->redirect && HTTP_STATUS_IS_REDIRECT(http_resp_->status_code)) {
                        std::string location = http_resp_->headers["Location"];
                        if (!location.empty()) {
                            hlogi("redirect %s => %s", http_req_->url.c_str(), location.c_str());
                            std::string ws_url = location;
                            if (hv::startswith(location, "http")) {
                                ws_url = hv::replace(location, "http", "ws");
                            }
                            // NOTE: not triggle onclose when redirecting.
                            channel->onclose = NULL;
                            open(ws_url.c_str());
                            return;
                        }
                    }
                    hloge("server side could not upgrade to websocket: status_code=%d", http_resp_->status_code);
                    channel->close();
                    return;
                }
                std::string ws_key = http_req_->GetHeader(SEC_WEBSOCKET_KEY);
                char ws_accept[32] = {0};
                ws_encode_key(ws_key.c_str(), ws_accept);
                std::string ws_accept2 = http_resp_->GetHeader(SEC_WEBSOCKET_ACCEPT);
                if (strcmp(ws_accept, ws_accept2.c_str()) != 0) {
                    hloge("Sec-WebSocket-Accept not match!");
                    channel->close();
                    return;
                }
                ws_parser_.reset(new WebSocketParser);
                // websocket_onmessage
                ws_parser_->onMessage = [this, &channel](int opcode, const std::string& msg) {
                    channel->opcode = (enum ws_opcode)opcode;
                    switch (opcode) {
                    case WS_OPCODE_CLOSE:
                        channel->close();
                        break;
                    case WS_OPCODE_PING:
                    {
                        // printf("recv ping\n");
                        // printf("send pong\n");
                        channel->sendPong();
                        break;
                    }
                    case WS_OPCODE_PONG:
                        // printf("recv pong\n");
                        ping_cnt = 0;
                        break;
                    case WS_OPCODE_TEXT:
                    case WS_OPCODE_BINARY:
                        if (onmessage) onmessage(msg);
                        break;
                    default:
                        break;
                    }
                };
                state = WS_OPENED;
                // ping
                if (ping_interval > 0) {
                    ping_cnt = 0;
                    channel->setHeartbeat(ping_interval, [this](){
                        auto& channel = this->channel;
                        if (channel == NULL) return;
                        if (ping_cnt++ == 3) {
                            hloge("websocket no pong!");
                            channel->close();
                            return;
                        }
                        // printf("send ping\n");
                        channel->sendPing();
                    });
                }
                if (onopen) onopen();
            }
        }
        if (state == WS_OPENED && size != 0) {
            int nparse = ws_parser_->FeedRecvData(data, size);
            if (nparse != size) {
                hloge("websocket parse error!");
                channel->close();
                return;
            }
        }
    };

    state = CONNECTING;
    start();
    return 0;
}

int WebSocketClient::close() {
    closesocket();
    state = WS_CLOSED;
    return 0;
}

int WebSocketClient::send(const std::string& msg) {
    return send(msg.c_str(), msg.size(), WS_OPCODE_TEXT);
}

int WebSocketClient::send(const char* buf, int len, enum ws_opcode opcode) {
    if (channel == NULL) return -1;
    return channel->send(buf, len, opcode);
}

}
