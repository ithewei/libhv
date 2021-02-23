#include "WebSocketClient.h"

#include "http_parser.h" // for http_parser_url
#include "base64.h"
#include "hlog.h"

namespace hv {

WebSocketClient::WebSocketClient()
    : TcpClientTmpl<WebSocketChannel>()
{
    state = WS_CLOSED;
}

WebSocketClient::~WebSocketClient() {
    close();
}

/*
 * ParseUrl => createsocket => start =>
 * TCP::onConnection => websocket_handshake => WS::onopen =>
 * TCP::onMessage => WebSocketParser => WS::onmessage =>
 * TCP::onConnection => WS::onclose
 */
int WebSocketClient::open(const char* _url) {
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
    http_parser_url parser;
    http_parser_url_init(&parser);
    http_parser_parse_url(url.c_str(), url.size(), 0, &parser);
    // scheme
    bool wss = !strncmp(url.c_str(), "wss", 3);
    // host
    std::string host = "127.0.0.1";
    if (parser.field_set & (1<<UF_HOST)) {
        host = url.substr(parser.field_data[UF_HOST].off, parser.field_data[UF_HOST].len);
    }
    // port
    int port = parser.port ? parser.port : wss ? DEFAULT_HTTPS_PORT : DEFAULT_HTTP_PORT;
    // path
    std::string path = "/";
    if (parser.field_set & (1<<UF_PATH)) {
        path = url.c_str() + parser.field_data[UF_PATH].off;
    }

    int connfd = createsocket(port, host.c_str());
    if (connfd < 0) {
        return connfd;
    }
    if (wss) {
        withTLS();
    }

    onConnection = [this](const WebSocketChannelPtr& channel) {
        if (channel->isConnected()) {
            state = CONNECTED;
            // websocket_handshake
            http_req_.reset(new HttpRequest);
            http_req_->method = HTTP_GET;
            // ws => http
            http_req_->url = "http" + url.substr(2, -1);
            http_req_->headers["Connection"] = "Upgrade";
            http_req_->headers["Upgrade"] = "websocket";
            // generate SEC_WEBSOCKET_KEY
            unsigned char rand_key[16] = {0};
            int *p = (int*)rand_key;
            for (int i = 0; i < 4; ++i, ++p) {
                *p = rand();
            }
            char ws_key[32] = {0};
            base64_encode(rand_key, 16, ws_key);
            http_req_->headers[SEC_WEBSOCKET_KEY] = ws_key;
            http_req_->headers[SEC_WEBSOCKET_VERSION] = "13";
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
        if (state == WS_UPGRADING) {
            int nparse = http_parser_->FeedRecvData((const char*)buf->data(), buf->size());
            if (nparse != buf->size()) {
                hloge("http parse error!");
                channel->close();
                return;
            }
            if (http_parser_->IsComplete()) {
                if (http_resp_->status_code != HTTP_STATUS_SWITCHING_PROTOCOLS) {
                    hloge("server side not support websockt!");
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
                    switch (opcode) {
                    case WS_OPCODE_CLOSE:
                        channel->close();
                        break;
                    case WS_OPCODE_PING:
                    {
                        // printf("recv ping\n");
                        // printf("send pong\n");
                        channel->write(WS_CLIENT_PONG_FRAME, WS_CLIENT_MIN_FRAME_SIZE);
                        break;
                    }
                    case WS_OPCODE_PONG:
                        // printf("recv pong\n");
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
                if (onopen) onopen();
            }
        } else {
            int nparse = ws_parser_->FeedRecvData((const char*)buf->data(), buf->size());
            if (nparse != buf->size()) {
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
    if (channel == NULL) return -1;
    channel->close();
    stop();
    state = WS_CLOSED;
    return 0;
}

int WebSocketClient::send(const std::string& msg) {
    if (channel == NULL) return -1;
    return channel->send(msg);
}

}
