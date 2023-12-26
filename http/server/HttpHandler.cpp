#include "HttpHandler.h"

#include "hversion.h"
#include "herr.h"
#include "hlog.h"
#include "htime.h"
#include "hurl.h"
#include "hasync.h" // import hv::async for http_async_handler

#include "httpdef.h"
#include "http2def.h"
#include "wsdef.h"

#include "http_page.h"

#include "EventLoop.h" // import hv::setInterval
using namespace hv;

#define MIN_HTTP_REQUEST        "GET / HTTP/1.1\r\n\r\n"
#define MIN_HTTP_REQUEST_LEN    14 // exclude CRLF

#define HTTP_100_CONTINUE_RESPONSE      "HTTP/1.1 100 Continue\r\n\r\n"
#define HTTP_100_CONTINUE_RESPONSE_LEN  25
#define HTTP_200_CONNECT_RESPONSE       "HTTP/1.1 200 Connection established\r\n\r\n"
#define HTTP_200_CONNECT_RESPONSE_LEN   39

HttpHandler::HttpHandler(hio_t* io) :
    protocol(HttpHandler::UNKNOWN),
    state(WANT_RECV),
    error(0),
    // flags
    ssl(0),
    keepalive(1),
    upgrade(0),
    proxy(0),
    proxy_connected(0),
    forward_proxy(0),
    reverse_proxy(0),
    ip{'\0'},
    port(0),
    pid(0),
    tid(0),
    // for http
    io(io),
    service(NULL),
    api_handler(NULL),
    // for websocket
    ws_service(NULL),
    last_send_ping_time(0),
    last_recv_pong_time(0),
    // for sendfile
    files(NULL),
    file(NULL),
    // for proxy
    proxy_port(0)
{
    // Init();
}

HttpHandler::~HttpHandler() {
    Close();
}

bool HttpHandler::Init(int http_version) {
    parser.reset(HttpParser::New(HTTP_SERVER, (enum http_version)http_version));
    if (parser == NULL) {
        return false;
    }
    req  = std::make_shared<HttpRequest>();
    resp = std::make_shared<HttpResponse>();
    if(http_version == 1) {
        protocol = HTTP_V1;
    } else if (http_version == 2) {
        protocol = HTTP_V2;
        resp->http_major = req->http_major = 2;
        resp->http_minor = req->http_minor = 0;
    }
    if (io) {
        hloop_t* loop = hevent_loop(io);
        pid = hloop_pid(loop);
        tid = hloop_tid(loop);
        writer = std::make_shared<HttpResponseWriter>(io, resp);
        writer->status = hv::SocketChannel::CONNECTED;
    } else {
        pid = hv_getpid();
        tid = hv_gettid();
    }
    parser->InitRequest(req.get());
    // NOTE: hook http_cb
    req->http_cb = [this](HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
        if (this->state == WANT_CLOSE) return;
        switch (state) {
        case HP_HEADERS_COMPLETE:
            if (this->error != 0) return;
            onHeadersComplete();
            break;
        case HP_BODY:
            if (this->error != 0) return;
            onBody(data, size);
            break;
        case HP_MESSAGE_COMPLETE:
            onMessageComplete();
            break;
        default:
            break;
        }
    };
    return true;
}

void HttpHandler::Reset() {
    state = WANT_RECV;
    error = 0;
    req->Reset();
    resp->Reset();
    ctx = NULL;
    api_handler = NULL;
    closeFile();
    if (writer) {
        writer->Begin();
        writer->onwrite = NULL;
        writer->onclose = NULL;
    }
    parser->InitRequest(req.get());
}

void HttpHandler::Close() {
    if (writer) {
        writer->status = hv::SocketChannel::DISCONNECTED;
    }

    if (api_handler && api_handler->state_handler) {
        if (parser && !parser->IsComplete()) {
            api_handler->state_handler(context(), HP_ERROR, NULL, 0);
        }
        return;
    }

    // close proxy
    closeProxy();

    // close file
    closeFile();

    // onclose
    if (protocol == HttpHandler::WEBSOCKET) {
        WebSocketOnClose();
    } else {
        if (writer && writer->onclose) {
            writer->onclose();
        }
    }
}

bool HttpHandler::SwitchHTTP2() {
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

bool HttpHandler::SwitchWebSocket() {
    if(!io) return false;

    protocol = WEBSOCKET;
    ws_parser  = std::make_shared<WebSocketParser>();
    ws_channel = std::make_shared<WebSocketChannel>(io, WS_SERVER);
    ws_parser->onMessage = [this](int opcode, const std::string& msg){
        ws_channel->opcode = (enum ws_opcode)opcode;
        switch(opcode) {
        case WS_OPCODE_CLOSE:
            ws_channel->send(msg, WS_OPCODE_CLOSE);
            ws_channel->close();
            break;
        case WS_OPCODE_PING:
            // printf("recv ping\n");
            // printf("send pong\n");
            ws_channel->send(msg, WS_OPCODE_PONG);
            break;
        case WS_OPCODE_PONG:
            // printf("recv pong\n");
            this->last_recv_pong_time = gethrtime_us();
            break;
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            // onmessage
            if (ws_service && ws_service->onmessage) {
                ws_service->onmessage(ws_channel, msg);
            }
            break;
        default:
            break;
        }
    };
    // NOTE: cancel keepalive timer, judge alive by heartbeat.
    ws_channel->setKeepaliveTimeout(0);
    if (ws_service && ws_service->ping_interval > 0) {
        int ping_interval = MAX(ws_service->ping_interval, 1000);
        ws_channel->setHeartbeat(ping_interval, [this](){
            if (last_recv_pong_time < last_send_ping_time) {
                hlogw("[%s:%d] websocket no pong!", ip, port);
                ws_channel->close();
            } else {
                // printf("send ping\n");
                ws_channel->sendPing();
                last_send_ping_time = gethrtime_us();
            }
        });
    }
    return true;
}

const HttpContextPtr& HttpHandler::context() {
    if (!ctx) {
        ctx = std::make_shared<hv::HttpContext>();
        ctx->service = service;
        ctx->request = req;
        ctx->response = resp;
        ctx->writer = writer;
    }
    return ctx;
}

int HttpHandler::customHttpHandler(const http_handler& handler) {
    return invokeHttpHandler(&handler);
}

int HttpHandler::invokeHttpHandler(const http_handler* handler) {
    int status_code = HTTP_STATUS_NOT_IMPLEMENTED;
    if (handler->sync_handler) {
        // NOTE: sync_handler run on IO thread
        status_code = handler->sync_handler(req.get(), resp.get());
    } else if (handler->async_handler) {
        // NOTE: async_handler run on hv::async threadpool
        hv::async(std::bind(handler->async_handler, req, writer));
        status_code = HTTP_STATUS_NEXT;
    } else if (handler->ctx_handler) {
        // NOTE: ctx_handler run on IO thread, you can easily post HttpContextPtr to your consumer thread for processing.
        status_code = handler->ctx_handler(context());
    } else if (handler->state_handler) {
        status_code = handler->state_handler(context(), HP_MESSAGE_COMPLETE, NULL, 0);
    }
    return status_code;
}

void HttpHandler::onHeadersComplete() {
    // printf("onHeadersComplete\n");
    int status_code = handleRequestHeaders();
    if (status_code != HTTP_STATUS_OK) {
        error = ERR_REQUEST;
        return;
    }

    HttpRequest* pReq = req.get();
    if (service && service->pathHandlers.size() != 0) {
        service->GetRoute(pReq, &api_handler);
    }

    if (api_handler && api_handler->state_handler) {
        api_handler->state_handler(context(), HP_HEADERS_COMPLETE, NULL, 0);
        return;
    }

    if (proxy) {
        handleProxy();
        return;
    }

    // Expect: 100-continue
    handleExpect100();
}

void HttpHandler::onBody(const char* data, size_t size) {
    if (api_handler && api_handler->state_handler) {
        api_handler->state_handler(context(), HP_BODY, data, size);
        return;
    }

    if (proxy && proxy_connected) {
        if (io) hio_write_upstream(io, (void*)data, size);
        return;
    }

    req->body.append(data, size);
    return;
}

void HttpHandler::onMessageComplete() {
    // printf("onMessageComplete\n");
    int status_code = HTTP_STATUS_OK;

    if (error) {
        SendHttpStatusResponse(resp->status_code);
        return;
    }

    if (proxy) {
        if (proxy_connected) Reset();
        return;
    }

    addResponseHeaders();

    // upgrade ? handleUpgrade : HandleHttpRequest
    if (upgrade) {
        auto iter_upgrade = req->headers.find("upgrade");
        if (iter_upgrade != req->headers.end()) {
            handleUpgrade(iter_upgrade->second.c_str());
            status_code = resp->status_code;
        }
    } else {
        status_code = HandleHttpRequest();
        if (status_code != HTTP_STATUS_NEXT) {
            SendHttpResponse();
        }
    }

    // access log
    if (service && service->enable_access_log) {
        hlogi("[%ld-%ld][%s:%d][%s %s]=>[%d %s]",
            pid, tid, ip, port,
            http_method_str(req->method), req->path.c_str(),
            resp->status_code, resp->status_message());
    }

    if (status_code != HTTP_STATUS_NEXT) {
        // keepalive ? Reset : Close
        if (keepalive) {
            Reset();
        } else {
            state = WANT_CLOSE;
        }
    }
}

int HttpHandler::handleRequestHeaders() {
    HttpRequest* pReq = req.get();
    pReq->scheme = ssl ? "https" : "http";
    pReq->client_addr.ip = ip;
    pReq->client_addr.port = port;

    // keepalive
    keepalive = pReq->IsKeepAlive();

    // upgrade
    upgrade = pReq->IsUpgrade();

    // proxy
    proxy = forward_proxy = reverse_proxy = 0;
    if (hv::startswith(pReq->url, "http")) {
        // forward proxy
        proxy = forward_proxy = 1;
    }
    else if (pReq->method == HTTP_CONNECT) {
        // proxy tunnel
        // CONNECT ip:port HTTP/1.1\r\n
        pReq->url = "https://" + pReq->url;
        proxy = forward_proxy = 1;
        keepalive = true;
    }

    // printf("url=%s\n", pReq->url.c_str());
    pReq->ParseUrl();
    // printf("path=%s\n",  pReq->path.c_str());
    // fix CVE-2023-26147
    if (pReq->path.find("%") != std::string::npos) {
        std::string unescaped_path = HUrl::unescape(pReq->path);
        if (unescaped_path.find("\r\n") != std::string::npos) {
            hlogw("Illegal path: %s\n",  unescaped_path.c_str());
            resp->status_code = HTTP_STATUS_BAD_REQUEST;
            return resp->status_code;
        }
    }

    if (proxy) {
        // Proxy-Connection
        auto iter = pReq->headers.find("Proxy-Connection");
        if (iter != pReq->headers.end()) {
            const char* keepalive_value = iter->second.c_str();
            if (stricmp(keepalive_value, "keep-alive") == 0) {
                keepalive = true;
            }
            else if (stricmp(keepalive_value, "close") == 0) {
                keepalive = false;
            }
            else if (stricmp(keepalive_value, "upgrade") == 0) {
                keepalive = true;
            }
        }
    }
    else {
        // reverse proxy
        std::string proxy_url = service->GetProxyUrl(pReq->path.c_str());
        if (!proxy_url.empty()) {
            pReq->url = proxy_url;
            proxy = reverse_proxy = 1;
        }
    }

    // TODO: rewrite url
    return HTTP_STATUS_OK;
}

void HttpHandler::handleExpect100() {
    // Expect: 100-continue
    auto iter = req->headers.find("Expect");
    if (iter != req->headers.end() &&
        stricmp(iter->second.c_str(), "100-continue") == 0) {
        if (io) hio_write(io, HTTP_100_CONTINUE_RESPONSE, HTTP_100_CONTINUE_RESPONSE_LEN);
    }
}

void HttpHandler::addResponseHeaders() {
    HttpResponse* pResp = resp.get();
    // Server:
    static char s_Server[64] = {'\0'};
    if (s_Server[0] == '\0') {
        snprintf(s_Server, sizeof(s_Server), "httpd/%s", hv_version());
    }
    pResp->headers["Server"] = s_Server;

    // Connection:
    pResp->headers["Connection"] = keepalive ? "keep-alive" : "close";
}

int HttpHandler::HandleHttpRequest() {
    // preprocessor -> middleware -> processor -> postprocessor
    HttpRequest* pReq = req.get();
    HttpResponse* pResp = resp.get();

    // NOTE: Not all users want to parse body, we comment it out.
    // pReq->ParseBody();

    int status_code = pResp->status_code;
    if (status_code != HTTP_STATUS_OK) {
        goto postprocessor;
    }

preprocessor:
    state = HANDLE_BEGIN;
    if (service->preprocessor) {
        status_code = customHttpHandler(service->preprocessor);
        if (status_code != HTTP_STATUS_NEXT) {
            goto postprocessor;
        }
    }

middleware:
    for (const auto& middleware : service->middleware) {
        status_code = customHttpHandler(middleware);
        if (status_code != HTTP_STATUS_NEXT) {
            goto postprocessor;
        }
    }

processor:
    if (service->processor) {
        status_code = customHttpHandler(service->processor);
    } else {
        status_code = defaultRequestHandler();
    }

postprocessor:
    if (status_code >= 100 && status_code < 600) {
        pResp->status_code = (http_status)status_code;
        if (pResp->status_code >= 400 && pResp->body.size() == 0 && pReq->method != HTTP_HEAD) {
            if (service->errorHandler) {
                customHttpHandler(service->errorHandler);
            } else {
                defaultErrorHandler();
            }
        }
    }
    if (fc) {
        pResp->content = fc->filebuf.base;
        pResp->content_length = fc->filebuf.len;
        pResp->headers["Content-Type"] = fc->content_type;
        pResp->headers["Last-Modified"] = fc->last_modified;
        pResp->headers["Etag"] = fc->etag;
    }
    if (service->postprocessor) {
        customHttpHandler(service->postprocessor);
    }

    if (writer && writer->state != hv::HttpResponseWriter::SEND_BEGIN) {
        status_code = HTTP_STATUS_NEXT;
    }
    if (status_code == HTTP_STATUS_NEXT) {
        state = HANDLE_CONTINUE;
    } else {
        state = HANDLE_END;
    }
    return status_code;
}

int HttpHandler::defaultRequestHandler() {
    int status_code = HTTP_STATUS_OK;

    if (api_handler) {
        status_code = invokeHttpHandler(api_handler);
    }
    else if (req->method == HTTP_GET || req->method == HTTP_HEAD) {
        // static handler
        if (service->staticHandler) {
            status_code = customHttpHandler(service->staticHandler);
        }
        else if (service->staticDirs.size() > 0) {
            status_code = defaultStaticHandler();
        }
        else {
            status_code = HTTP_STATUS_NOT_FOUND;
        }
    }
    else {
        // Not Implemented
        status_code = HTTP_STATUS_NOT_IMPLEMENTED;
    }

    return status_code;
}

int HttpHandler::defaultStaticHandler() {
    // file service
    std::string path = req->Path();
    const char* req_path = path.c_str();
    // path safe check
    if (req_path[0] != '/' || strstr(req_path, "/..") || strstr(req_path, "\\..")) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    std::string filepath;
    bool is_dir = path.back() == '/' &&
                  service->index_of.size() > 0 &&
                  hv_strstartswith(req_path, service->index_of.c_str());
    if (is_dir) {
        filepath = service->document_root + path;
    } else {
        filepath = service->GetStaticFilepath(req_path);
    }
    if (filepath.empty()) {
        return HTTP_STATUS_NOT_FOUND;
    }

    int status_code = HTTP_STATUS_OK;
    // Range:
    bool has_range = false;
    long from, to = 0;
    if (req->GetRange(from, to)) {
        has_range = true;
        if (openFile(filepath.c_str()) != 0) {
            return HTTP_STATUS_NOT_FOUND;
        }
        long total = file->size();
        if (to == 0 || to >= total) to = total - 1;
        file->seek(from);
        status_code = HTTP_STATUS_PARTIAL_CONTENT;
        resp->status_code = HTTP_STATUS_PARTIAL_CONTENT;
        resp->content_length = to - from + 1;
        resp->SetContentTypeByFilename(filepath.c_str());
        resp->SetRange(from, to, total);
        if(resp->content_length < service->max_file_cache_size) {
            // read into body directly
            int nread = file->readrange(resp->body, from, to);
            closeFile();
            if (nread != resp->content_length) {
                resp->content_length = 0;
                resp->body.clear();
                return HTTP_STATUS_INTERNAL_SERVER_ERROR;
            }
        }
        else {
            if (service->largeFileHandler) {
                status_code = customHttpHandler(service->largeFileHandler);
            } else {
                status_code = defaultLargeFileHandler();
            }
        }
        return status_code;
    }

    // FileCache
    FileCache::OpenParam param;
    param.max_read = service->max_file_cache_size;
    param.need_read = !(req->method == HTTP_HEAD || has_range);
    param.path = req_path;
    if (files) {
        fc = files->Open(filepath.c_str(), &param);
    }
    if (fc == NULL) {
        if (param.error == ERR_OVER_LIMIT) {
            if (service->largeFileHandler) {
                status_code = customHttpHandler(service->largeFileHandler);
            } else {
                status_code = defaultLargeFileHandler();
            }
        } else {
            status_code = HTTP_STATUS_NOT_FOUND;
        }
    }
    else {
        // Not Modified
        auto iter = req->headers.find("if-not-match");
        if (iter != req->headers.end() &&
            strcmp(iter->second.c_str(), fc->etag) == 0) {
            fc = NULL;
            return HTTP_STATUS_NOT_MODIFIED;
        }

        iter = req->headers.find("if-modified-since");
        if (iter != req->headers.end() &&
            strcmp(iter->second.c_str(), fc->last_modified) == 0) {
            fc = NULL;
            return HTTP_STATUS_NOT_MODIFIED;
        }
    }
    return status_code;
}

int HttpHandler::defaultLargeFileHandler() {
    if (!writer) return HTTP_STATUS_NOT_IMPLEMENTED;
    if (!isFileOpened()) {
        std::string filepath = service->GetStaticFilepath(req->Path().c_str());
        if (filepath.empty() || openFile(filepath.c_str()) != 0) {
            return HTTP_STATUS_NOT_FOUND;
        }
        resp->content_length = file->size();
        resp->SetContentTypeByFilename(filepath.c_str());
    }
    if (service->limit_rate == 0) {
        // forbidden to send large file
        resp->content_length = 0;
        resp->status_code = HTTP_STATUS_FORBIDDEN;
    } else {
        size_t bufsize = 40960; // 40K
        file->buf.resize(bufsize);
        if (service->limit_rate < 0) {
            // unlimited: sendFile when writable
            writer->onwrite = [this](HBuf* buf) {
                if (writer->isWriteComplete()) {
                    sendFile();
                }
            };
        } else {
            // limit_rate=40KB/s  interval_ms=1000
            // limit_rate=500KB/s interval_ms=80
            int interval_ms = file->buf.len * 1000 / 1024 / service->limit_rate;
            // limit_rate=40MB/s interval_m=1: 40KB/ms = 40MB/s = 320Mbps
            if (interval_ms == 0) interval_ms = 1;
            // printf("limit_rate=%dKB/s interval_ms=%d\n", service->limit_rate, interval_ms);
            file->timer = setInterval(interval_ms, std::bind(&HttpHandler::sendFile, this));
        }
    }
    writer->EndHeaders();
    return HTTP_STATUS_UNFINISHED;
}

int HttpHandler::defaultErrorHandler() {
    // error page
    if (service->error_page.size() != 0) {
        std::string filepath = service->document_root + '/' + service->error_page;
        if (files) {
            // cache and load error page
            FileCache::OpenParam param;
            fc = files->Open(filepath.c_str(), &param);
        }
    }
    // status page
    if (fc == NULL && resp->body.size() == 0) {
        resp->content_type = TEXT_HTML;
        make_http_status_page(resp->status_code, resp->body);
    }
    return 0;
}

int HttpHandler::FeedRecvData(const char* data, size_t len) {
    if (protocol == HttpHandler::UNKNOWN) {
        int http_version = 1;
#if WITH_NGHTTP2
        if (strncmp(data, HTTP2_MAGIC, MIN(len, HTTP2_MAGIC_LEN)) == 0) {
            http_version = 2;
        }
#else
        // check request-line
        if (len < MIN_HTTP_REQUEST_LEN) {
            hloge("[%s:%d] http request-line too small", ip, port);
            error = ERR_REQUEST;
            return -1;
        }
        for (int i = 0; i < MIN_HTTP_REQUEST_LEN; ++i) {
            if (!IS_GRAPH(data[i])) {
                hloge("[%s:%d] http request-line not plain", ip, port);
                error = ERR_REQUEST;
                return -1;
            }
        }
#endif
        if (!Init(http_version)) {
            hloge("[%s:%d] unsupported HTTP%d", ip, port, http_version);
            error = ERR_INVALID_PROTOCOL;
            return -1;
        }
    }

    int nfeed = 0;
    switch (protocol) {
    case HttpHandler::HTTP_V1:
    case HttpHandler::HTTP_V2:
        if (state != WANT_RECV) {
            Reset();
        }
        nfeed = parser->FeedRecvData(data, len);
        // printf("FeedRecvData %d=>%d\n", (int)len, nfeed);
        if (nfeed != len) {
            hloge("[%s:%d] http parse error: %s", ip, port, parser->StrError(parser->GetError()));
            error = ERR_PARSE;
            return -1;
        }
        break;
    case HttpHandler::WEBSOCKET:
        nfeed = ws_parser->FeedRecvData(data, len);
        if (nfeed != len) {
            hloge("[%s:%d] websocket parse error!", ip, port);
            error = ERR_PARSE;
            return -1;
        }
        break;
    default:
        hloge("[%s:%d] unknown protocol", ip, port);
        error = ERR_INVALID_PROTOCOL;
        return -1;
    }

    if (state == WANT_CLOSE) return 0;
    return error ? -1 : nfeed;
}

int HttpHandler::GetSendData(char** data, size_t* len) {
    if (state == HANDLE_CONTINUE) {
        return 0;
    }

    HttpRequest* pReq = req.get();
    HttpResponse* pResp = resp.get();

    if (protocol == HTTP_V1) {
        switch(state) {
        case WANT_RECV:
            if (parser->IsComplete()) state = WANT_SEND;
            else return 0;
        case HANDLE_END:
             state = WANT_SEND;
        case WANT_SEND:
            state = SEND_HEADER;
        case SEND_HEADER:
        {
            size_t content_length = 0;
            const char* content = NULL;
            // HEAD
            if (pReq->method == HTTP_HEAD) {
                if (fc) {
                    pResp->headers["Accept-Ranges"] = "bytes";
                    pResp->headers["Content-Length"] = hv::to_string(fc->st.st_size);
                } else {
                    pResp->headers["Content-Type"] = "text/html";
                    pResp->headers["Content-Length"] = "0";
                }
                state = SEND_DONE;
                goto return_nobody;
            }
            // File service
            if (fc) {
                // FileCache
                // NOTE: no copy filebuf, more efficient
                header = pResp->Dump(true, false);
                fc->prepend_header(header.c_str(), header.size());
                *data = fc->httpbuf.base;
                *len = fc->httpbuf.len;
                state = SEND_DONE;
                return *len;
            }
            // API service
            content_length = pResp->ContentLength();
            content = (const char*)pResp->Content();
            if (content) {
                if (content_length > (1 << 20)) {
                    state = SEND_BODY;
                    goto return_header;
                } else {
                    // NOTE: header+body in one package if <= 1M
                    header = pResp->Dump(true, false);
                    header.append(content, content_length);
                    state = SEND_DONE;
                    goto return_header;
                }
            } else {
                state = SEND_DONE;
                goto return_header;
            }
return_nobody:
            pResp->content_length = 0;
return_header:
            if (header.empty()) header = pResp->Dump(true, false);
            *data = (char*)header.c_str();
            *len = header.size();
            return *len;
        }
        case SEND_BODY:
        {
            *data = (char*)pResp->Content();
            *len = pResp->ContentLength();
            state = SEND_DONE;
            return *len;
        }
        case SEND_DONE:
        {
            // NOTE: remove file cache if > FILE_CACHE_MAX_SIZE
            if (fc && fc->filebuf.len > FILE_CACHE_MAX_SIZE) {
                files->Close(fc);
            }
            fc = NULL;
            header.clear();
            return 0;
        }
        default:
            return 0;
        }
    } else if (protocol == HTTP_V2) {
        int ret = parser->GetSendData(data, len);
        if (ret == 0) state = SEND_DONE;
        return ret;
    }
    return 0;
}

int HttpHandler::SendHttpResponse(bool submit) {
    if (!io || !parser) return -1;
    char* data = NULL;
    size_t len = 0, total_len = 0;
    if (submit) parser->SubmitResponse(resp.get());
    while (GetSendData(&data, &len)) {
        // printf("GetSendData %d\n", (int)len);
        if (data && len) {
            hio_write(io, data, len);
            total_len += len;
        }
    }
    return total_len;
}

int HttpHandler::SendHttpStatusResponse(http_status status_code) {
    if (state > WANT_SEND) return 0;
    resp->status_code = status_code;
    addResponseHeaders();
    HandleHttpRequest();
    state = WANT_SEND;
    return SendHttpResponse();
}

//------------------sendfile--------------------------------------
int HttpHandler::openFile(const char* filepath) {
    closeFile();
    file = new LargeFile;
    file->timer = INVALID_TIMER_ID;
    return file->open(filepath, "rb");
}

bool HttpHandler::isFileOpened() {
    return file && file->isopen();
}

int HttpHandler::sendFile() {
    if (!writer || !writer->isWriteComplete() ||
        !isFileOpened() ||
        file->buf.len == 0 ||
        resp->content_length == 0) {
        return -1;
    }

    int readbytes = MIN(file->buf.len, resp->content_length);
    size_t nread = file->read(file->buf.base, readbytes);
    if (nread <= 0) {
        hloge("read file error!");
        error = ERR_READ_FILE;
        writer->close(true);
        return nread;
    }
    int nwrite = writer->WriteBody(file->buf.base, nread);
    if (nwrite < 0) {
        // disconnectd
        writer->close(true);
        return nwrite;
    }
    resp->content_length -= nread;
    if (resp->content_length == 0) {
        writer->End();
        closeFile();
    }
    return nread;
}

void HttpHandler::closeFile() {
    if (file) {
        if (file->timer != INVALID_TIMER_ID) {
            killTimer(file->timer);
            file->timer = INVALID_TIMER_ID;
        }
        delete file;
        file = NULL;
    }
}

//------------------upgrade--------------------------------------
int HttpHandler::handleUpgrade(const char* upgrade_protocol) {
    hlogi("[%s:%d] Upgrade: %s", ip, port, upgrade_protocol);

    // websocket
    if (stricmp(upgrade_protocol, "websocket") == 0) {
        return upgradeWebSocket();
    }

    // h2/h2c
    if (strnicmp(upgrade_protocol, "h2", 2) == 0) {
        return upgradeHTTP2();
    }

    hloge("[%s:%d] unsupported Upgrade: %s", ip, port, upgrade_protocol);
    return SetError(ERR_INVALID_PROTOCOL);
}

int HttpHandler::upgradeWebSocket() {
    /*
    HTTP/1.1 101 Switching Protocols
    Connection: Upgrade
    Upgrade: websocket
    Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
    */
    resp->status_code = HTTP_STATUS_SWITCHING_PROTOCOLS;
    resp->headers["Connection"] = "Upgrade";
    resp->headers["Upgrade"] = "websocket";

    // Sec-WebSocket-Accept:
    auto iter_key = req->headers.find(SEC_WEBSOCKET_KEY);
    if (iter_key != req->headers.end()) {
        char ws_accept[32] = {0};
        ws_encode_key(iter_key->second.c_str(), ws_accept);
        resp->headers[SEC_WEBSOCKET_ACCEPT] = ws_accept;
    }

    // Sec-WebSocket-Protocol:
    auto iter_protocol = req->headers.find(SEC_WEBSOCKET_PROTOCOL);
    if (iter_protocol != req->headers.end()) {
        hv::StringList subprotocols = hv::split(iter_protocol->second, ',');
        if (subprotocols.size() > 0) {
            hlogw("%s: %s => just select first protocol %s", SEC_WEBSOCKET_PROTOCOL, iter_protocol->second.c_str(), subprotocols[0].c_str());
            resp->headers[SEC_WEBSOCKET_PROTOCOL] = subprotocols[0];
        }
    }

    SendHttpResponse();

    if (!SwitchWebSocket()) {
        hloge("[%s:%d] unsupported websocket", ip, port);
        return SetError(ERR_INVALID_PROTOCOL);
    }

    // onopen
    WebSocketOnOpen();
    return 0;
}

int HttpHandler::upgradeHTTP2() {
    /*
    HTTP/1.1 101 Switching Protocols
    Connection: Upgrade
    Upgrade: h2c
    */
    resp->status_code = HTTP_STATUS_SWITCHING_PROTOCOLS;
    resp->headers["Connection"] = "Upgrade";
    resp->headers["Upgrade"] = "h2c";

    SendHttpResponse();

    if (!SwitchHTTP2()) {
        hloge("[%s:%d] unsupported HTTP2", ip, port);
        return SetError(ERR_INVALID_PROTOCOL);
    }

    // NOTE: send HTTP2_SETTINGS frame
    SendHttpResponse(false);

    return 0;
}

//------------------proxy--------------------------------------
int HttpHandler::handleProxy() {
    if (forward_proxy) {
        return handleForwardProxy();
    }

    if (reverse_proxy) {
        return handleReverseProxy();
    }

    return 0;
}

int HttpHandler::handleForwardProxy() {
    if (service && service->enable_forward_proxy) {
        return connectProxy(req->url);
    } else {
        hlogw("Forbidden to forward proxy %s", req->url.c_str());
        SetError(HTTP_STATUS_FORBIDDEN, HTTP_STATUS_FORBIDDEN);
    }
    return 0;
}

int HttpHandler::handleReverseProxy() {
    return connectProxy(req->url);
}

int HttpHandler::connectProxy(const std::string& strUrl) {
    if (!io) return ERR_NULL_POINTER;

    HUrl url;
    url.parse(strUrl);
    hlogi("[%s:%d] proxy_pass %s", ip, port, strUrl.c_str());

    if (proxy_connected) {
        if (url.host == proxy_host && url.port == proxy_port) {
            // reuse keepalive connection
            sendProxyRequest();
            return 0;
        } else {
            // detach and close previous connection
            hio_t* upstream_io = hio_get_upstream(io);
            if (upstream_io) {
                hio_setcb_close(upstream_io, NULL);
                closeProxy();
            }
        }
    }

    if (forward_proxy && !service->IsTrustProxy(url.host.c_str())) {
        hlogw("Forbidden to proxy %s", url.host.c_str());
        SetError(HTTP_STATUS_FORBIDDEN, HTTP_STATUS_FORBIDDEN);
        return 0;
    }

    hloop_t* loop = hevent_loop(io);
    proxy = 1;
    proxy_host = url.host;
    proxy_port = url.port;
    hio_t* upstream_io = hio_create_socket(loop, proxy_host.c_str(), proxy_port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (upstream_io == NULL) {
        return SetError(ERR_SOCKET, HTTP_STATUS_BAD_GATEWAY);
    }
    if (url.scheme == "https") {
        hio_enable_ssl(upstream_io);
    }
    hevent_set_userdata(upstream_io, this);
    hio_setup_upstream(io, upstream_io);
    hio_setcb_connect(upstream_io, HttpHandler::onProxyConnect);
    hio_setcb_close(upstream_io, HttpHandler::onProxyClose);
    if (service->proxy_connect_timeout > 0) {
        hio_set_connect_timeout(upstream_io, service->proxy_connect_timeout);
    }
    if (service->proxy_read_timeout > 0) {
        hio_set_read_timeout(io, service->proxy_read_timeout);
    }
    if (service->proxy_write_timeout > 0) {
        hio_set_write_timeout(io, service->proxy_write_timeout);
    }
    hio_connect(upstream_io);
    // NOTE: wait upstream_io connected then start read
    hio_read_stop(io);
    return 0;
}

int HttpHandler::closeProxy() {
    if (proxy && proxy_connected) {
        proxy_connected = 0;
        if (io) hio_close_upstream(io);
    }
    return 0;
}

int HttpHandler::sendProxyRequest() {
    if (!io || !proxy_connected) return -1;

    req->headers.erase("Host");
    req->FillHost(proxy_host.c_str(), proxy_port);
    req->headers.erase("Proxy-Connection");
    req->headers["Connection"] = keepalive ? "keep-alive" : "close";
    req->headers["X-Real-IP"] = ip;
    // NOTE: send head + received body
    std::string msg = req->Dump(true, true);
    // printf("%s\n", msg.c_str());
    req->Reset();

    hio_write_upstream(io, (void*)msg.c_str(), msg.size());
    if (parser->IsComplete()) state = WANT_SEND;
    return msg.size();
}

void HttpHandler::onProxyConnect(hio_t* upstream_io) {
    // printf("onProxyConnect\n");
    HttpHandler* handler = (HttpHandler*)hevent_userdata(upstream_io);
    hio_t* io = hio_get_upstream(upstream_io);
    assert(handler != NULL && io != NULL);
    handler->proxy_connected = 1;

    if (handler->req->method == HTTP_CONNECT) {
        // handler->resp->status_code = HTTP_STATUS_OK;
        // handler->SendHttpResponse();
        hio_write(io, HTTP_200_CONNECT_RESPONSE, HTTP_200_CONNECT_RESPONSE_LEN);
        handler->state = SEND_DONE;
        // NOTE: recv request then upstream
        hio_setcb_read(io, hio_write_upstream);
    } else {
        handler->sendProxyRequest();
    }

    // NOTE: start recv request continue then upstream
    if (handler->upgrade) hio_setcb_read(io, hio_write_upstream);
    hio_read_start(io);
    // NOTE: start recv response then upstream
    hio_setcb_read(upstream_io, hio_write_upstream);
    hio_read_start(upstream_io);
}

void HttpHandler::onProxyClose(hio_t* upstream_io) {
    // printf("onProxyClose\n");
    HttpHandler* handler = (HttpHandler*)hevent_userdata(upstream_io);
    if (handler == NULL) return;
    handler->proxy_connected = 0;

    hevent_set_userdata(upstream_io, NULL);

    int error = hio_error(upstream_io);
    if (error == ETIMEDOUT) {
        handler->SendHttpStatusResponse(HTTP_STATUS_GATEWAY_TIMEOUT);
    }

    handler->error = error;
    hio_close_upstream(upstream_io);
}
