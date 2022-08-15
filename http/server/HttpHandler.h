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

    // flags
    unsigned ssl:           1;
    unsigned keepalive:     1;
    unsigned proxy:         1;

    // peeraddr
    char                    ip[64];
    int                     port;

    // for http
    HttpService             *service;
    HttpRequestPtr          req;
    HttpResponsePtr         resp;
    HttpResponseWriterPtr   writer;
    HttpParserPtr           parser;
    HttpContextPtr          ctx;
    http_handler*           api_handler;

    // for sendfile
    FileCache               *files;
    file_cache_ptr          fc; // cache small file
    struct LargeFile : public HFile {
        HBuf                buf;
        uint64_t            timer;
    } *file; // for large file

    // for GetSendData
    std::string             header;
    // std::string          body;

    // for websocket
    WebSocketService*           ws_service;
    WebSocketChannelPtr         ws_channel;
    WebSocketParserPtr          ws_parser;
    uint64_t                    last_send_ping_time;
    uint64_t                    last_recv_pong_time;

    HttpHandler();
    ~HttpHandler();

    bool Init(int http_version = 1, hio_t* io = NULL);
    void Reset();

    int FeedRecvData(const char* data, size_t len);
    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleHttpRequest();
    int GetSendData(char** data, size_t* len);

    // HTTP2
    bool SwitchHTTP2();

    // websocket
    bool SwitchWebSocket(hio_t* io = NULL);
    void WebSocketOnOpen() {
        ws_channel->status = hv::SocketChannel::CONNECTED;
        if (ws_service && ws_service->onopen) {
            ws_service->onopen(ws_channel, req);
        }
    }
    void WebSocketOnClose() {
        ws_channel->status = hv::SocketChannel::DISCONNECTED;
        if (ws_service && ws_service->onclose) {
            ws_service->onclose(ws_channel);
        }
    }

private:
    int  openFile(const char* filepath);
    int  sendFile();
    void closeFile();
    bool isFileOpened();

    const HttpContextPtr& getHttpContext();
    void initRequest();
    void onHeadersComplete();

    // proxy
    int proxyConnect(const std::string& url);
    static void onProxyConnect(hio_t* upstream_io);

    int defaultRequestHandler();
    int defaultStaticHandler();
    int defaultLargeFileHandler();
    int defaultErrorHandler();
    int customHttpHandler(const http_handler& handler);
    int invokeHttpHandler(const http_handler* handler);
};

#endif // HV_HTTP_HANDLER_H_
