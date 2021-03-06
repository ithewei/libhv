#include "HttpHandler.h"

#include "hbase.h"
#include "http_page.h"

int HttpHandler::HandleHttpRequest() {
    // preprocessor -> api -> web -> postprocessor

    int ret = 0;
    http_api_handler api = NULL;

    req.ParseUrl();
    req.client_addr.ip = ip;
    req.client_addr.port = port;

preprocessor:
    if (service->preprocessor) {
        ret = service->preprocessor(&req, &res);
        if (ret != 0) {
            goto make_http_status_page;
        }
    }

    if (service->api_handlers.size() != 0) {
        service->GetApi(&req, &api);
    }

    if (api) {
        // api service
        ret = api(&req, &res);
        if (ret != 0) {
            goto make_http_status_page;
        }
    }
    else if (service->document_root.size() != 0 &&
            (req.method == HTTP_GET || req.method == HTTP_HEAD)) {
        // web service
        // path safe check
        const char* req_path = req.path.c_str();
        if (*req_path != '/' || strstr(req_path, "/../")) {
            res.status_code = HTTP_STATUS_BAD_REQUEST;
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
            bool need_read = req.method == HTTP_HEAD ? false : true;
            fc = files->Open(filepath.c_str(), need_read, (void*)req_path);
        }
        if (fc == NULL) {
            // Not Found
            ret = HTTP_STATUS_NOT_FOUND;
        }
        else {
            // Not Modified
            auto iter = req.headers.find("if-not-match");
            if (iter != req.headers.end() &&
                strcmp(iter->second.c_str(), fc->etag) == 0) {
                ret = HTTP_STATUS_NOT_MODIFIED;
                fc = NULL;
            }
            else {
                iter = req.headers.find("if-modified-since");
                if (iter != req.headers.end() &&
                    strcmp(iter->second.c_str(), fc->last_modified) == 0) {
                    ret = HTTP_STATUS_NOT_MODIFIED;
                    fc = NULL;
                }
            }
        }
    }
    else {
        // Not Implemented
        ret = HTTP_STATUS_NOT_IMPLEMENTED;
    }

make_http_status_page:
    if (ret >= 100 && ret < 600) {
        res.status_code = (http_status)ret;
    }
    if (res.status_code >= 400 && res.body.size() == 0 && req.method != HTTP_HEAD) {
        // error page
        if (service->error_page.size() != 0) {
            std::string filepath = service->document_root;
            filepath += '/';
            filepath += service->error_page;
            fc = files->Open(filepath.c_str(), true, NULL);
        }
        // status page
        if (fc == NULL && res.body.size() == 0) {
            res.content_type = TEXT_HTML;
            make_http_status_page(res.status_code, res.body);
        }
    }

    if (fc) {
        res.content = fc->filebuf.base;
        res.content_length = fc->filebuf.len;
        if (fc->content_type && *fc->content_type != '\0') {
            res.headers["Content-Type"] = fc->content_type;
        }
        res.headers["Last-Modified"] = fc->last_modified;
        res.headers["Etag"] = fc->etag;
    }

postprocessor:
    if (service->postprocessor) {
        ret = service->postprocessor(&req, &res);
    }

    state = WANT_SEND;
    return ret;
}

int HttpHandler::GetSendData(char** data, size_t* len) {
    if (protocol == HTTP_V1) {
        switch(state) {
        case WANT_RECV:
            if (parser->IsComplete()) state = WANT_SEND;
            else return 0;
        case WANT_SEND:
            state = SEND_HEADER;
        case SEND_HEADER:
        {
            int content_length = 0;
            const char* content = NULL;
            // HEAD
            if (req.method == HTTP_HEAD) {
                if (fc) {
                    res.headers["Accept-Ranges"] = "bytes";
                    res.headers["Content-Length"] = hv::to_string(fc->st.st_size);
                } else {
                    res.headers["Content-Type"] = "text/html";
                    res.headers["Content-Length"] = "0";
                }
                state = SEND_DONE;
                goto return_nobody;
            }
            // File service
            if (fc) {
                long from, to, total;
                int nread;
                // Range:
                if (req.GetRange(from, to)) {
                    HFile file;
                    if (file.open(fc->filepath.c_str(), "rb") != 0) {
                        res.status_code = HTTP_STATUS_NOT_FOUND;
                        state = SEND_DONE;
                        goto return_nobody;
                    }
                    total = file.size();
                    if (to == 0 || to >= total) to = total - 1;
                    res.content_length = to - from + 1;
                    nread = file.readrange(body, from, to);
                    if (nread != res.content_length) {
                        res.status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
                        state = SEND_DONE;
                        goto return_nobody;
                    }
                    res.SetRange(from, to, total);
                    state = SEND_BODY;
                    goto return_header;
                }
                // FileCache
                // NOTE: no copy filebuf, more efficient
                header = res.Dump(true, false);
                fc->prepend_header(header.c_str(), header.size());
                *data = fc->httpbuf.base;
                *len = fc->httpbuf.len;
                state = SEND_DONE;
                return *len;
            }
            // API service
            content_length = res.ContentLength();
            content = (const char*)res.Content();
            if (content) {
                if (content_length > (1 << 20)) {
                    state = SEND_BODY;
                    goto return_header;
                } else {
                    // NOTE: header+body in one package if <= 1M
                    header = res.Dump(true, false);
                    header.append(content, content_length);
                    state = SEND_DONE;
                    goto return_header;
                }
            } else {
                state = SEND_DONE;
                goto return_header;
            }
return_nobody:
            res.content_length = 0;
return_header:
            if (header.empty()) header = res.Dump(true, false);
            *data = (char*)header.c_str();
            *len = header.size();
            return *len;
        }
        case SEND_BODY:
        {
            if (body.empty()) {
                *data = (char*)res.Content();
                *len = res.ContentLength();
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
