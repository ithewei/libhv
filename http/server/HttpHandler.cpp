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

    if (fc) {
        // link file cache
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

postprocessor:
    if (service->postprocessor) {
        ret = service->postprocessor(&req, &res);
    }

    return ret;
}
