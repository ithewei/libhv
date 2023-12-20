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
        WANT_CLOSE,
    } state;

    // errno
    int                     error;

    // flags
    unsigned ssl                :1;
    unsigned keepalive          :1;
    unsigned upgrade            :1;
    unsigned proxy              :1;
    unsigned proxy_connected    :1;
    unsigned forward_proxy      :1;
    unsigned reverse_proxy      :1;

    // peeraddr
    char                    ip[64];
    int                     port;

    // for log
    long                    pid;
    long                    tid;

    // for http
    hio_t                   *io;
    HttpService             *service;
    HttpRequestPtr          req;
    HttpResponsePtr         resp;
    HttpResponseWriterPtr   writer;
    HttpParserPtr           parser;
    HttpContextPtr          ctx;
    http_handler*           api_handler;

    // for GetSendData
    std::string             header;
    // std::string          body;

    // for websocket
    WebSocketService*       ws_service;
    WebSocketChannelPtr     ws_channel;
    WebSocketParserPtr      ws_parser;
    uint64_t                last_send_ping_time;
    uint64_t                last_recv_pong_time;

    // for sendfile
    FileCache               *files;
    file_cache_ptr          fc;     // cache small file
    struct LargeFile : public HFile {
        HBuf        buf;
        uint64_t    timer;
    }                       *file;  // for large file

    // for proxy
    std::string             proxy_host;
    int                     proxy_port;

    HttpHandler(hio_t* io = NULL);
    ~HttpHandler();

    bool Init(int http_version = 1);
    void Reset();
    void Close();

    /* @workflow:
     * HttpServer::on_recv -> HttpHandler::FeedRecvData -> Init -> HttpParser::InitRequest -> HttpRequest::http_cb ->
     * onHeadersComplete -> proxy ? handleProxy -> connectProxy :
     * onMessageComplete -> upgrade ? handleUpgrade : HandleHttpRequest -> HttpParser::SubmitResponse ->
     * SendHttpResponse -> while(GetSendData) hio_write ->
     * keepalive ? Reset : Close -> hio_close
     *
     * @return
     * == len:   ok
     * == 0:     WANT_CLOSE
     * <  0:     error
     */
    int FeedRecvData(const char* data, size_t len);

    /* @workflow:
     * preprocessor -> middleware -> processor -> postprocessor
     *
     * @return status_code
     * == 0:    HANDLE_CONTINUE
     * != 0:    HANDLE_END
     */
    int HandleHttpRequest();

    int GetSendData(char** data, size_t* len);

    int SendHttpResponse(bool submit = true);
    int SendHttpStatusResponse(http_status status_code);

    // HTTP2
    bool SwitchHTTP2();

    // websocket
    bool SwitchWebSocket();
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

    int SetError(int error_code, http_status status_code = HTTP_STATUS_BAD_REQUEST) {
        error = error_code;
        if (resp) resp->status_code = status_code;
        return error;
    }

private:
    const HttpContextPtr& context();
    int   handleRequestHeaders();
    // Expect: 100-continue
    void  handleExpect100();
    void  addResponseHeaders();

    // http_cb
    void onHeadersComplete();
    void onBody(const char* data, size_t size);
    void onMessageComplete();

    // default handlers
    int defaultRequestHandler();
    int defaultStaticHandler();
    int defaultLargeFileHandler();
    int defaultErrorHandler();
    int customHttpHandler(const http_handler& handler);
    int invokeHttpHandler(const http_handler* handler);

    // sendfile
    int  openFile(const char* filepath);
    int  sendFile();
    void closeFile();
    bool isFileOpened();

    // upgrade
    int handleUpgrade(const char* upgrade_protocol);
    int upgradeWebSocket();
    int upgradeHTTP2();

    // proxy
    int handleProxy();
    int handleForwardProxy();
    int handleReverseProxy();
    int connectProxy(const std::string& url);
    int closeProxy();
    int sendProxyRequest();
    static void onProxyConnect(hio_t* upstream_io);
    static void onProxyClose(hio_t* upstream_io);
};

#endif // HV_HTTP_HANDLER_H_
