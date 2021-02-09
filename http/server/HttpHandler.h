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

    // peeraddr
    char                    ip[64];
    int                     port;

    // for http
    HttpService             *service;
    FileCache               *files;
    file_cache_t            *fc;

    HttpRequest             req;
    HttpResponse            res;
    HttpParserPtr           parser;

    // for websocket
    WebSocketHandlerPtr         ws;
    WebSocketServerCallbacks*   ws_cbs;

    HttpHandler() {
        protocol = UNKNOWN;
        service = NULL;
        files = NULL;
        fc = NULL;
        ws_cbs = NULL;
    }

    void Reset() {
        req.Reset();
        res.Reset();
        fc = NULL;
    }

    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleHttpRequest();

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
