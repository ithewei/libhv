#include "HttpHandler.h"

#include "hbase.h"
#include "http_page.h"

int HttpHandler::HandleRequest() {
    // preprocessor -> api -> web -> postprocessor
    // preprocessor
    req.ParseUrl();
    req.client_addr.ip = ip;
    req.client_addr.port = port;
    if (service->preprocessor) {
        if (service->preprocessor(&req, &res) == HANDLE_DONE) {
            return HANDLE_DONE;
        }
    }

    http_api_handler api = NULL;
    int ret = 0;
    if (service->api_handlers.size() != 0) {
        ret = service->GetApi(&req, &api);
    }

    if (api) {
        // api service
        if (api(&req, &res) == HANDLE_DONE) {
            return HANDLE_DONE;
        }
    }
    else if (ret == HTTP_STATUS_METHOD_NOT_ALLOWED) {
        // Method Not Allowed
        res.status_code = HTTP_STATUS_METHOD_NOT_ALLOWED;
    }
    else if (service->document_root.size() != 0 && req.method == HTTP_GET) {
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
            fc = files->Open(filepath.c_str(), (void*)req_path);
        }
        if (fc == NULL) {
            // Not Found
            res.status_code = HTTP_STATUS_NOT_FOUND;
        }
        else {
            // Not Modified
            auto iter = req.headers.find("if-not-match");
            if (iter != req.headers.end() &&
                strcmp(iter->second.c_str(), fc->etag) == 0) {
                res.status_code = HTTP_STATUS_NOT_MODIFIED;
                fc = NULL;
            }
            else {
                iter = req.headers.find("if-modified-since");
                if (iter != req.headers.end() &&
                    strcmp(iter->second.c_str(), fc->last_modified) == 0) {
                    res.status_code = HTTP_STATUS_NOT_MODIFIED;
                    fc = NULL;
                }
            }
        }
    }
    else {
        // Not Implemented
        res.status_code = HTTP_STATUS_NOT_IMPLEMENTED;
    }

make_http_status_page:
    // html page
    if (res.status_code >= 400 && res.body.size() == 0) {
        // error page
        if (service->error_page.size() != 0) {
            std::string filepath = service->document_root;
            filepath += '/';
            filepath += service->error_page;
            fc = files->Open(filepath.c_str(), NULL);
        }
        // status page
        if (fc == NULL && res.body.size() == 0) {
            res.content_type = TEXT_HTML;
            make_http_status_page(res.status_code, res.body);
        }
    }

    // file
    if (fc) {
        res.content = (unsigned char*)fc->filebuf.base;
        res.content_length = fc->filebuf.len;
        if (fc->content_type && *fc->content_type != '\0') {
            res.headers["Content-Type"] = fc->content_type;
            res.FillContentType();
        }
        char sz[64];
        snprintf(sz, sizeof(sz), "%d", res.content_length);
        res.headers["Content-Length"] = sz;
        res.headers["Last-Modified"] = fc->last_modified;
        res.headers["Etag"] = fc->etag;
    }

    // postprocessor
    if (service->postprocessor) {
        if (service->postprocessor(&req, &res) == HANDLE_DONE) {
            return HANDLE_DONE;
        }
    }
    return HANDLE_DONE;
}
