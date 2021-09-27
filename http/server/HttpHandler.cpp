#include "HttpHandler.h"

#include "hbase.h"
#include "herr.h"
#include "hlog.h"
#include "http_page.h"

int HttpHandler::HandleHttpRequest() {
    // preprocessor -> processor -> postprocessor
    int status_code = HTTP_STATUS_OK;
    HttpRequest* pReq = req.get();
    HttpResponse* pResp = resp.get();

    pReq->scheme = ssl ? "https" : "http";
    pReq->client_addr.ip = ip;
    pReq->client_addr.port = port;
    pReq->Host();
    pReq->ParseUrl();
    // pReq->ParseBody();

preprocessor:
    state = HANDLE_BEGIN;
    if (service->preprocessor) {
        status_code = service->preprocessor(pReq, pResp);
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
        service->postprocessor(pReq, pResp);
    }

    if (status_code == 0) {
        state = HANDLE_CONTINUE;
    } else {
        state = HANDLE_END;
        parser->SubmitResponse(resp.get());
    }
    return status_code;
}

int HttpHandler::customHttpHandler(http_handler& fn) {
    HttpContextPtr ctx(new hv::HttpContext);
    ctx->service = service;
    ctx->request = req;
    ctx->response = resp;
    ctx->writer = writer;
    return fn(ctx);
}

int HttpHandler::defaultRequestHandler() {
    int status_code = HTTP_STATUS_OK;
    http_sync_handler sync_handler = NULL;
    http_async_handler async_handler = NULL;
    http_handler ctx_handler = NULL;

    if (service->api_handlers.size() != 0) {
        service->GetApi(req.get(), &sync_handler, &async_handler, &ctx_handler);
    }

    if (sync_handler) {
        // sync api handler
        status_code = sync_handler(req.get(), resp.get());
    }
    else if (async_handler) {
        // async api handler
        async_handler(req, writer);
        status_code = 0;
    }
    else if (ctx_handler) {
        // HttpContext handler
        status_code = customHttpHandler(ctx_handler);
        if (writer->state != hv::HttpResponseWriter::SEND_BEGIN) {
            status_code = 0;
        }
    }
    else if (req->method == HTTP_GET || req->method == HTTP_HEAD) {
        // static handler
        if (service->staticHandler) {
            status_code = customHttpHandler(service->staticHandler);
        }
        else if (service->document_root.size() != 0) {
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
    int status_code = HTTP_STATUS_OK;
    std::string path = req->Path();
    const char* req_path = path.c_str();
    // path safe check
    if (req_path[0] != '/' || strstr(req_path, "/../")) {
        return HTTP_STATUS_BAD_REQUEST;
    }
    std::string filepath = service->document_root + path;
    if (req_path[1] == '\0') {
        filepath += service->home_page;
    }
    bool is_dir = filepath.c_str()[filepath.size()-1] == '/';
    bool is_index_of = false;
    if (service->index_of.size() != 0 && strstartswith(req_path, service->index_of.c_str())) {
        is_index_of = true;
    }
    if (!is_dir || is_index_of) {
        FileCache::OpenParam param;
        param.need_read = req->method == HTTP_HEAD ? false : true;
        param.path = req_path;
        fc = files->Open(filepath.c_str(), &param);
        if (fc == NULL) {
            status_code = HTTP_STATUS_NOT_FOUND;
            if (param.error == ERR_OVER_LIMIT) {
                if (service->largeFileHandler) {
                    status_code = customHttpHandler(service->largeFileHandler);
                }
            }
        }
    } else {
        status_code = HTTP_STATUS_NOT_FOUND;
    }

    if (fc) {
        // Not Modified
        auto iter = req->headers.find("if-not-match");
        if (iter != req->headers.end() &&
            strcmp(iter->second.c_str(), fc->etag) == 0) {
            status_code = HTTP_STATUS_NOT_MODIFIED;
            fc = NULL;
        }
        else {
            iter = req->headers.find("if-modified-since");
            if (iter != req->headers.end() &&
                strcmp(iter->second.c_str(), fc->last_modified) == 0) {
                status_code = HTTP_STATUS_NOT_MODIFIED;
                fc = NULL;
            }
        }
    }
    return status_code;
}

int HttpHandler::defaultErrorHandler() {
    // error page
    if (service->error_page.size() != 0) {
        std::string filepath = service->document_root;
        filepath += '/';
        filepath += service->error_page;
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
        nfeed = ws->parser->FeedRecvData(data, len);
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
            int content_length = 0;
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
                long from, to, total;
                int nread;
                // Range:
                if (pReq->GetRange(from, to)) {
                    HFile file;
                    if (file.open(fc->filepath.c_str(), "rb") != 0) {
                        pResp->status_code = HTTP_STATUS_NOT_FOUND;
                        state = SEND_DONE;
                        goto return_nobody;
                    }
                    total = file.size();
                    if (to == 0 || to >= total) to = total - 1;
                    pResp->content_length = to - from + 1;
                    nread = file.readrange(body, from, to);
                    if (nread != pResp->content_length) {
                        pResp->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
                        state = SEND_DONE;
                        goto return_nobody;
                    }
                    pResp->SetRange(from, to, total);
                    state = SEND_BODY;
                    goto return_header;
                }
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
            if (body.empty()) {
                *data = (char*)pResp->Content();
                *len = pResp->ContentLength();
            } else {
                *data = (char*)body.c_str();
                *len = body.size();
            }
            state = SEND_DONE;
            return *len;
        }
        case SEND_DONE:
        {
            // NOTE: remove file cache if > 16M
            if (fc && fc->filebuf.len > (1 << 24)) {
                files->Close(fc);
            }
            fc = NULL;
            header.clear();
            body.clear();
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
