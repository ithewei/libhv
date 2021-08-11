#ifndef HV_HTTP_HANDLER_H_
#define HV_HTTP_HANDLER_H_

#include "HttpService.h"
#include "HttpParser.h"
#include "FileCache.h"

#include "WebSocketServer.h"
#include "WebSocketParser.h"

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
        HANDLE_BEGIN,
        HANDLE_CONTINUE,
        HANDLE_END,
        WANT_SEND,
        SEND_HEADER,
        SEND_BODY,
        SEND_DONE,
    } state;

    // peeraddr
    bool                    ssl;
    char                    ip[64];
    int                     port;

    // for http
    HttpService             *service;
    FileCache               *files;

    HttpRequestPtr          req;
    HttpResponsePtr         resp;
    HttpResponseWriterPtr   writer;
    HttpParserPtr           parser;

    // for GetSendData
    file_cache_ptr          fc;
    std::string             header;
    std::string             body;

    // for websocket
    WebSocketHandlerPtr         ws;
    WebSocketService*           ws_service;

    HttpHandler() {
        protocol = UNKNOWN;
        state = WANT_RECV;
        ssl = false;
        service = NULL;
        files = NULL;
        ws_service = NULL;
    }

    bool Init(int http_version = 1) {
        parser.reset(HttpParser::New(HTTP_SERVER, (enum http_version)http_version));
        if (parser == NULL) {
            return false;
        }
        protocol = http_version == 1 ? HTTP_V1 : HTTP_V2;
        req.reset(new HttpRequest);
        resp.reset(new HttpResponse);
        if (http_version == 2) {
            req->http_major = 2;
            req->http_minor = 0;
            resp->http_major = 2;
            resp->http_minor = 0;
        }
        parser->InitRequest(req.get());
        return true;
    }

    bool SwitchHTTP2() {
        parser.reset(HttpParser::New(HTTP_SERVER, ::HTTP_V2));
        if (parser == NULL) {
            return false;
        }
        protocol = HTTP_V2;
        req->http_major = 2;
        req->http_minor = 0;
        resp->http_major = 2;
        resp->http_minor = 0;
        parser->InitRequest(req.get());
        return true;
    }

    void Reset() {
        state = WANT_RECV;
        req->Reset();
        resp->Reset();
        parser->InitRequest(req.get());
    }

    int FeedRecvData(const char* data, size_t len);
    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleHttpRequest();
    int GetSendData(char** data, size_t* len);

    // websocket
    WebSocketHandler* SwitchWebSocket() {
        ws.reset(new WebSocketHandler);
        protocol = WEBSOCKET;
        return ws.get();
    }
    void WebSocketOnOpen() {
        ws->onopen();
        if (ws_service && ws_service->onopen) {
            ws_service->onopen(ws->channel, req->url);
        }
    }
    void WebSocketOnClose() {
        ws->onclose();
        if (ws_service && ws_service->onclose) {
            ws_service->onclose(ws->channel);
        }
    }
    void WebSocketOnMessage(const std::string& msg) {
        if (ws_service && ws_service->onmessage) {
            ws_service->onmessage(ws->channel, msg);
        }
    }

private:
    int defaultRequestHandler();
    int defaultStaticHandler();
    int defaultErrorHandler();
    int customHttpHandler(http_handler& fn);
};

#endif // HV_HTTP_HANDLER_H_
