#ifndef HV_HTTP_HANDLER_H_
#define HV_HTTP_HANDLER_H_

#include "HttpService.h"
#include "HttpParser.h"
#include "FileCache.h"

#include "WebSocketServer.h"
#include "WebSocketParser.h"

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
    // std::string             body;

    // for websocket
    WebSocketChannelPtr         ws_channel;
    WebSocketParserPtr          ws_parser;
    uint64_t                    last_send_ping_time;
    uint64_t                    last_recv_pong_time;
    WebSocketService*           ws_service;

    HttpHandler();
    ~HttpHandler();

    bool Init(int http_version = 1, hio_t* io = NULL) {
        parser.reset(HttpParser::New(HTTP_SERVER, (enum http_version)http_version));
        if (parser == NULL) {
            return false;
        }
        req.reset(new HttpRequest);
        resp.reset(new HttpResponse);
        if (http_version == 2) {
            protocol = HTTP_V2;
            resp->http_major = req->http_major = 2;
            resp->http_minor = req->http_minor = 0;
        }
        else if(http_version == 1) {
            protocol = HTTP_V1;
        }
        parser->InitRequest(req.get());
        if (io) {
            // shared resp object with HttpResponseWriter
            writer.reset(new hv::HttpResponseWriter(io, resp));
            writer->status = hv::SocketChannel::CONNECTED;
            writer->onwrite = std::bind(&HttpHandler::onWrite, this, std::placeholders::_1);
        }
        return true;
    }

    bool SwitchHTTP2() {
        parser.reset(HttpParser::New(HTTP_SERVER, ::HTTP_V2));
        if (parser == NULL) {
            return false;
        }
        protocol = HTTP_V2;
        resp->http_major = req->http_major = 2;
        resp->http_minor = req->http_minor = 0;
        parser->InitRequest(req.get());
        return true;
    }

    void Reset() {
        state = WANT_RECV;
        req->Reset();
        resp->Reset();
        parser->InitRequest(req.get());
        if (writer) {
            writer->Begin();
        }
        resetFlush();
    }

    int FeedRecvData(const char* data, size_t len);
    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleHttpRequest();
    int GetSendData(char** data, size_t* len);

    // websocket
    bool SwitchWebSocket(hio_t* io, ws_session_type type = WS_SERVER);

    void WebSocketOnOpen() {
        ws_channel->status = hv::SocketChannel::CONNECTED;
        if (ws_service && ws_service->onopen) {
            ws_service->onopen(ws_channel, req->url);
        }
    }
    void WebSocketOnClose() {
        ws_channel->status = hv::SocketChannel::DISCONNECTED;
        if (ws_service && ws_service->onclose) {
            ws_service->onclose(ws_channel);
        }
    }

private:
    HFile file; ///< file cache body
    uint64_t flush_timer;
    bool flushing_;
    int last_flush_size;
    uint64_t last_flush_time;
    void flushFile();
    void resetFlush();
    void onWrite(hv::Buffer* buf);

    int defaultRequestHandler();
    int defaultStaticHandler();
    int defaultErrorHandler();
    int customHttpHandler(const http_handler& handler);
    int invokeHttpHandler(const http_handler* handler);
};

#endif // HV_HTTP_HANDLER_H_
