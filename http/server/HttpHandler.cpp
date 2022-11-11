#include "HttpHandler.h"

#include "hbase.h"
#include "herr.h"
#include "hlog.h"
#include "htime.h"
#include "hurl.h"
#include "hasync.h" // import hv::async for http_async_handler
#include "http_page.h"

#include "EventLoop.h" // import hv::setInterval
using namespace hv;

HttpHandler::HttpHandler() {
    protocol = UNKNOWN;
    state = WANT_RECV;
    ssl = false;
    keepalive = true;
    proxy = false;
    service = NULL;
    ws_service = NULL;
    api_handler = NULL;
    last_send_ping_time = 0;
    last_recv_pong_time = 0;

    files = NULL;
    file = NULL;
}

HttpHandler::~HttpHandler() {
    closeFile();
    if (writer) {
        writer->status = hv::SocketChannel::DISCONNECTED;
    }
}

bool HttpHandler::Init(int http_version, hio_t* io) {
    parser.reset(HttpParser::New(HTTP_SERVER, (enum http_version)http_version));
    if (parser == NULL) {
        return false;
    }
    req.reset(new HttpRequest);
    resp.reset(new HttpResponse);
    if(http_version == 1) {
        protocol = HTTP_V1;
    } else if (http_version == 2) {
        protocol = HTTP_V2;
        resp->http_major = req->http_major = 2;
        resp->http_minor = req->http_minor = 0;
    }
    if (io) {
        writer.reset(new hv::HttpResponseWriter(io, resp));
        writer->status = hv::SocketChannel::CONNECTED;
    }
    initRequest();
    return true;
}

void HttpHandler::Reset() {
    state = WANT_RECV;
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
    initRequest();
}

void HttpHandler::initRequest() {
    parser->InitRequest(req.get());
    // NOTE: hook http_cb
    req->http_cb = [this](HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
        if (state == HP_HEADERS_COMPLETE) {
            onHeadersComplete();
        } else if (state == HP_BODY) {
            if (api_handler && api_handler->state_handler) {
                // goto cb;
            } else {
                msg->body.append(data, size);
            }
        }
        if (api_handler && api_handler->state_handler) {
// cb:
            api_handler->state_handler(getHttpContext(), state, data, size);
        }
    };
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

bool HttpHandler::SwitchWebSocket(hio_t* io) {
    if (!io && writer) io = writer->io();
    if(!io) return false;

    protocol = WEBSOCKET;
    ws_parser.reset(new WebSocketParser);
    ws_channel.reset(new hv::WebSocketChannel(io, WS_SERVER));
    ws_parser->onMessage = [this](int opcode, const std::string& msg){
        ws_channel->opcode = (enum ws_opcode)opcode;
        switch(opcode) {
        case WS_OPCODE_CLOSE:
            ws_channel->close();
            break;
        case WS_OPCODE_PING:
            // printf("recv ping\n");
            // printf("send pong\n");
            ws_channel->sendPong();
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
    if (ws_service && ws_service->ping_interval > 0) {
        // NOTE: cancel keepalive timer, judge alive by heartbeat.
        ws_channel->setKeepaliveTimeout(0);
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

const HttpContextPtr& HttpHandler::getHttpContext() {
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
        status_code = HTTP_STATUS_UNFINISHED;
    } else if (handler->ctx_handler) {
        // NOTE: ctx_handler run on IO thread, you can easily post HttpContextPtr to your consumer thread for processing.
        status_code = handler->ctx_handler(getHttpContext());
    } else if (handler->state_handler) {
        status_code = resp->status_code;
    }
    return status_code;
}

void HttpHandler::onHeadersComplete() {
    HttpRequest* pReq = req.get();
    pReq->scheme = ssl ? "https" : "http";
    pReq->client_addr.ip = ip;
    pReq->client_addr.port = port;

    // keepalive
    keepalive = pReq->IsKeepAlive();

    // NOTE: Detect proxy before ParseUrl
    proxy = 0;
    if (hv::startswith(pReq->url, "http")) {
        // forward proxy
        proxy = 1;
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

    // printf("url=%s\n", pReq->url.c_str());
    pReq->ParseUrl();

    if (service->api_handlers.size() != 0) {
        service->GetApi(pReq, &api_handler);
    }
    if (api_handler && api_handler->state_handler) {
        writer->onclose = [this](){
            // HP_ERROR
            if (!parser->IsComplete()) {
                if (api_handler && api_handler->state_handler) {
                    api_handler->state_handler(getHttpContext(), HP_ERROR, NULL, 0);
                }
            }
        };
    } else {
        if (proxy) {
            // forward proxy
            if (service->enable_forward_proxy) {
                proxyConnect(pReq->url);
            } else {
                proxy = 0;
                resp->status_code = HTTP_STATUS_FORBIDDEN;
                hlogw("Forbidden to forward proxy %s", pReq->url.c_str());
            }
        }
        else if (service->proxies.size() != 0) {
            // reverse proxy
            std::string proxy_url = service->GetProxyUrl(pReq->path.c_str());
            if (!proxy_url.empty()) {
                proxy = 1;
                pReq->url = proxy_url;
                proxyConnect(pReq->url);
            }
        }
        else {
            // TODO: rewrite
        }
    }
}

void HttpHandler::onProxyConnect(hio_t* upstream_io) {
    // printf("onProxyConnect\n");
    HttpHandler* handler = (HttpHandler*)hevent_userdata(upstream_io);
    hio_t* io = hio_get_upstream(upstream_io);
    assert(handler != NULL && io != NULL);

    HttpRequest* req = handler->req.get();
    // NOTE: send head + received body
    req->headers.erase("Proxy-Connection");
    req->headers["Connection"] = handler->keepalive ? "keep-alive" : "close";
    req->headers["X-Real-IP"] = handler->ip;
    std::string msg = req->Dump(true, true);
    // printf("%s\n", msg.c_str());
    hio_write(upstream_io, msg.c_str(), msg.size());

    // NOTE: start recv body continue then upstream
    hio_setcb_read(io, hio_write_upstream);
    hio_read_start(io);
    hio_setcb_read(upstream_io, hio_write_upstream);
    hio_read_start(upstream_io);
}

int HttpHandler::proxyConnect(const std::string& strUrl) {
    if (!writer) return ERR_NULL_POINTER;
    hio_t* io = writer->io();
    hloop_t* loop = hevent_loop(io);

    HUrl url;
    if (!url.parse(strUrl)) {
        return ERR_PARSE;
    }

    hlogi("proxy_pass %s", strUrl.c_str());
    hio_t* upstream_io = hio_create_socket(loop, url.host.c_str(), url.port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (upstream_io == NULL) {
        hio_close_async(io);
        return ERR_SOCKET;
    }
    if (url.scheme == "https") {
        hio_enable_ssl(upstream_io);
    }
    hevent_set_userdata(upstream_io, this);
    hio_setup_upstream(io, upstream_io);
    hio_setcb_connect(upstream_io, HttpHandler::onProxyConnect);
    hio_setcb_close(upstream_io, hio_close_upstream);
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

int HttpHandler::HandleHttpRequest() {
    // preprocessor -> processor -> postprocessor
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
    if (service->allow_cors) {
        resp->headers["Access-Control-Allow-Origin"] = req->GetHeader("Origin", "*");
        if (req->method == HTTP_OPTIONS) {
            resp->headers["Access-Control-Allow-Methods"] = req->GetHeader("Access-Control-Request-Method", "OPTIONS, HEAD, GET, POST, PUT, DELETE, PATCH");
            resp->headers["Access-Control-Allow-Headers"] = req->GetHeader("Access-Control-Request-Headers", "Content-Type");
            status_code = HTTP_STATUS_NO_CONTENT;
            goto postprocessor;
        }
    }
    if (service->preprocessor) {
        status_code = customHttpHandler(service->preprocessor);
        if (status_code != 0) {
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
    }
    if (pResp->status_code >= 400 && pResp->body.size() == 0 && pReq->method != HTTP_HEAD) {
        if (service->errorHandler) {
            customHttpHandler(service->errorHandler);
        } else {
            defaultErrorHandler();
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
        status_code = 0;
    }
    if (status_code == 0) {
        state = HANDLE_CONTINUE;
    } else {
        state = HANDLE_END;
        parser->SubmitResponse(resp.get());
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
    if (req_path[0] != '/' || strstr(req_path, "/../")) {
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
    fc = files->Open(filepath.c_str(), &param);
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
        // cache and load error page
        FileCache::OpenParam param;
        fc = files->Open(filepath.c_str(), &param);
    }
    // status page
    if (fc == NULL && resp->body.size() == 0) {
        resp->content_type = TEXT_HTML;
        make_http_status_page(resp->status_code, resp->body);
    }
    return 0;
}

int HttpHandler::FeedRecvData(const char* data, size_t len) {
    int nfeed = 0;
    if (protocol == HttpHandler::WEBSOCKET) {
        nfeed = ws_parser->FeedRecvData(data, len);
        if (nfeed != len) {
            hloge("[%s:%d] websocket parse error!", ip, port);
        }
    } else {
        if (state != WANT_RECV) {
            Reset();
        }
        nfeed = parser->FeedRecvData(data, len);
        if (nfeed != len) {
            hloge("[%s:%d] http parse error: %s", ip, port, parser->StrError(parser->GetError()));
        }
    }
    return nfeed;
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
        return parser->GetSendData(data, len);
    }
    return 0;
}

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
