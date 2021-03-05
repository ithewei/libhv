#ifndef HTTP_HANDLER_H_
#define HTTP_HANDLER_H_

#include "HttpService.h"
#include "HttpParser.h"
#include "FileCache.h"

#include "WebSocketServer.h"
#include "WebSocketParser.h"

#include "hlog.h"

class WebSocketHandler {
public:
    WebSocketChannelPtr         channel;
    WebSocketParserPtr          parser;
    uint64_t                    last_send_ping_time;
    uint64_t                    last_recv_pong_time;

    WebSocketHandler() {
        parser.reset(new WebSocketParser);
        // channel.reset(new WebSocketChannel);
        last_send_ping_time = 0;
        last_recv_pong_time = 0;
    }

    void onopen() {
        channel->status = hv::SocketChannel::CONNECTED;
        /*
        channel->onread = [this](hv::Buffer* buf) {
            const char* data = (const char*)buf->data();
            int size= buf->size();
            int nfeed = parser->FeedRecvData(data, size);
            if (nfeed != size) {
                hloge("websocket parse error!");
                channel->close();
            }
        };
        */
    }

    void onclose() {
        channel->status = hv::SocketChannel::DISCONNECTED;
    }
};
typedef std::shared_ptr<WebSocketHandler> WebSocketHandlerPtr;

class HttpHandler {
public:
    enum ProtocolType {
        UNKNOWN,
        HTTP_V1,
        HTTP_V2,
        WEBSOCKET,
    } protocol;
    enum State {
        WANT_RECV,
        WANT_SEND,
        SEND_HEADER,
        SEND_BODY,
        SEND_DONE,
    } state;

    // peeraddr
    char                    ip[64];
    int                     port;

    // for http
    HttpService             *service;
    FileCache               *files;

    HttpRequest             req;
    HttpResponse            res;
    HttpParserPtr           parser;

    // for GetSendData
    file_cache_ptr          fc;
    std::string             header;
    std::string             body;

    // for websocket
    WebSocketHandlerPtr         ws;
    WebSocketServerCallbacks*   ws_cbs;

    HttpHandler() {
        protocol = UNKNOWN;
        state = WANT_RECV;
        service = NULL;
        files = NULL;
        ws_cbs = NULL;
    }

    void Reset() {
        state = WANT_RECV;
        req.Reset();
        res.Reset();
    }

    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleHttpRequest();
    int GetSendData(char** data, size_t* len);

    // websocket
    WebSocketHandler* SwitchWebSocket() {
        ws.reset(new WebSocketHandler);
        return ws.get();
    }
    void WebSocketOnOpen() {
        ws->onopen();
        if (ws_cbs && ws_cbs->onopen) {
            ws_cbs->onopen(ws->channel, req.url);
        }
    }
    void WebSocketOnClose() {
        ws->onclose();
        if (ws_cbs && ws_cbs->onclose) {
            ws_cbs->onclose(ws->channel);
        }
    }
    void WebSocketOnMessage(const std::string& msg) {
        if (ws_cbs && ws_cbs->onmessage) {
            ws_cbs->onmessage(ws->channel, msg);
        }
    }
};

#endif // HTTP_HANDLER_H_
