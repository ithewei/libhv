#ifndef HTTP_HANDLER_H_
#define HTTP_HANDLER_H_

#include "HttpService.h"
#include "HttpParser.h"
#include "FileCache.h"

/*
<!DOCTYPE html>
<html>
<head>
  <title>404 Not Found</title>
</head>
<body>
  <center><h1>404 Not Found</h1></center>
  <hr>
</body>
</html>
 */
static inline void make_http_status_page(http_status status_code, std::string& page) {
    char szCode[8];
    snprintf(szCode, sizeof(szCode), "%d ", status_code);
    const char* status_message = http_status_str(status_code);
    page += R"(<!DOCTYPE html>
<html>
<head>
  <title>)";
    page += szCode; page += status_message;
    page += R"(</title>
</head>
<body>
  <center><h1>)";
    page += szCode; page += status_message;
    page += R"(</h1></center>
  <hr>
</body>
</html>)";
}

class HttpHandler {
public:
    HttpService*            service;
    FileCache*              files;
    char                    srcip[64];
    int                     srcport;
    HttpParser              parser;
    HttpRequest             req;
    HttpResponse            res;
    file_cache_t*           fc;

    HttpHandler() {
        service = NULL;
        files = NULL;
        init();
    }

    void init() {
        parser.parser_request_init(&req);
        req.init();
        res.init();
        fc = NULL;
    }

    int handle_request() {
        // preprocessor -> api -> web -> postprocessor
        // preprocessor
        if (service->preprocessor) {
            if (service->preprocessor(&req, &res) == HANDLE_DONE) {
                return HANDLE_DONE;
            }
        }
        http_api_handler api = NULL;
        int ret = service->GetApi(req.url.c_str(), req.method, &api);
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
        else if (req.method == HTTP_GET) {
            // web service
            std::string filepath = service->document_root;
            filepath += req.url.c_str();
            if (strcmp(req.url.c_str(), "/") == 0) {
                filepath += service->home_page;
            }
            fc = files->Open(filepath.c_str());
            // Not Found
            if (fc == NULL) {
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

        // html page
        if (res.status_code >= 400 && res.body.size() == 0) {
            // error page
            if (service->error_page.size() != 0) {
                std::string filepath = service->document_root;
                filepath += '/';
                filepath += service->error_page;
                fc = files->Open(filepath.c_str());
            }
            // status page
            if (fc == NULL && res.body.size() == 0) {
                res.content_type = TEXT_HTML;
                make_http_status_page(res.status_code, res.body);
            }
        }

        // file
        if (fc) {
            if (fc->content_type && *fc->content_type != '\0') {
                res.headers["Content-Type"] = fc->content_type;
            }
            res.headers["Content-Length"] = std::to_string(fc->filebuf.len);
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
};

#endif // HTTP_HANDLER_H_
