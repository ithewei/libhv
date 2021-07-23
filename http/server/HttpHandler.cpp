#include "HttpHandler.h"

#include "hbase.h"
#include "http_page.h"

int HttpHandler::HandleHttpRequest() {
    // preprocessor -> api -> web -> postprocessor

    int status_code = HTTP_STATUS_OK;
    http_sync_handler sync_handler = NULL;
    http_async_handler async_handler = NULL;

    HttpRequest* pReq = req.get();
    HttpResponse* pResp = resp.get();

    pReq->scheme = ssl ? "https" : "http";
    pReq->client_addr.ip = ip;
    pReq->client_addr.port = port;
    pReq->Host();
    pReq->ParseUrl();

preprocessor:
    state = HANDLE_BEGIN;
    if (service->preprocessor) {
        status_code = service->preprocessor(pReq, pResp);
        if (status_code != 0) {
            goto make_http_status_page;
        }
    }

    if (service->api_handlers.size() != 0) {
        service->GetApi(pReq, &sync_handler, &async_handler);
    }

    if (sync_handler) {
        // sync api service
        status_code = sync_handler(pReq, pResp);
        if (status_code != 0) {
            goto make_http_status_page;
        }
    }
    else if (async_handler) {
        // async api service
        async_handler(req, writer);
        status_code = 0;
    }
    else if (service->document_root.size() != 0 &&
            (pReq->method == HTTP_GET || pReq->method == HTTP_HEAD)) {
        // file service
        status_code = 200;
        std::string path = pReq->Path();
        const char* req_path = path.c_str();
        // path safe check
        if (*req_path != '/' || strstr(req_path, "/../")) {
            pResp->status_code = HTTP_STATUS_BAD_REQUEST;
            goto make_http_status_page;
        }
        std::string filepath = service->document_root;
        filepath += req_path;
        if (req_path[1] == '\0') {
            filepath += service->home_page;
        }
        bool is_dir = filepath.c_str()[filepath.size()-1] == '/';
        bool is_index_of = false;
        if (service->index_of.size() != 0 && strstartswith(req_path, service->index_of.c_str())) {
            is_index_of = true;
        }
        if (!is_dir || is_index_of) {
            bool need_read = pReq->method == HTTP_HEAD ? false : true;
            fc = files->Open(filepath.c_str(), need_read, (void*)req_path);
        }
        if (fc == NULL) {
            // Not Found
            status_code = HTTP_STATUS_NOT_FOUND;
        }
        else {
            // Not Modified
            auto iter = pReq->headers.find("if-not-match");
            if (iter != pReq->headers.end() &&
                strcmp(iter->second.c_str(), fc->etag) == 0) {
                status_code = HTTP_STATUS_NOT_MODIFIED;
                fc = NULL;
            }
            else {
                iter = pReq->headers.find("if-modified-since");
                if (iter != pReq->headers.end() &&
                    strcmp(iter->second.c_str(), fc->last_modified) == 0) {
                    status_code = HTTP_STATUS_NOT_MODIFIED;
                    fc = NULL;
                }
            }
        }
    }
    else {
        // Not Implemented
        status_code = HTTP_STATUS_NOT_IMPLEMENTED;
    }

make_http_status_page:
    if (status_code >= 100 && status_code < 600) {
        pResp->status_code = (http_status)status_code;
    }
    if (pResp->status_code >= 400 && pResp->body.size() == 0 && pReq->method != HTTP_HEAD) {
        // error page
        if (service->error_page.size() != 0) {
            std::string filepath = service->document_root;
            filepath += '/';
            filepath += service->error_page;
            fc = files->Open(filepath.c_str(), true, NULL);
        }
        // status page
        if (fc == NULL && pResp->body.size() == 0) {
            pResp->content_type = TEXT_HTML;
            make_http_status_page(pResp->status_code, pResp->body);
        }
    }

    if (fc) {
        pResp->content = fc->filebuf.base;
        pResp->content_length = fc->filebuf.len;
        pResp->headers["Content-Type"] = fc->content_type;
        pResp->headers["Last-Modified"] = fc->last_modified;
        pResp->headers["Etag"] = fc->etag;
    }

postprocessor:
    if (service->postprocessor) {
        service->postprocessor(pReq, pResp);
    }

    if (status_code == 0) {
        state = HANDLE_CONTINUE;
    } else {
        state = HANDLE_END;
        parser->SubmitResponse(pResp);
    }
    return status_code;
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
