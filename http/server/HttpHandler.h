#ifndef HTTP_HANDLER_H_
#define HTTP_HANDLER_H_

#include "HttpService.h"
#include "HttpParser.h"
#include "FileCache.h"
#include "http_page.h"
#include "hloop.h"

#define HTTP_KEEPALIVE_TIMEOUT  75 // s

static inline void on_keepalive_timeout(htimer_t* timer) {
    hio_t* io = (hio_t*)hevent_userdata(timer);
    hio_close(io);
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
    hio_t*                  io;
    htimer_t*               keepalive_timer;

    HttpHandler() {
        service = NULL;
        files = NULL;
        io = NULL;
        keepalive_timer = NULL;
        init();
    }

    ~HttpHandler() {
        if (keepalive_timer) {
            htimer_del(keepalive_timer);
            keepalive_timer = NULL;
        }
    }

    void init() {
        fc = NULL;
        parser.parser_request_init(&req);
    }

    void reset() {
        init();
        req.reset();
        res.reset();
    }

    void keepalive() {
        if (keepalive_timer == NULL) {
            keepalive_timer = htimer_add(hevent_loop(io), on_keepalive_timeout, HTTP_KEEPALIVE_TIMEOUT*1000, 1);
            hevent_set_userdata(keepalive_timer, io);
        }
        else {
            htimer_reset(keepalive_timer);
        }
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
            if (filepath.c_str()[filepath.size()-1] != '/' ||
                (service->index_of.size() != 0 &&
                 req.url.size() >= service->index_of.size() &&
                 strnicmp(req.url.c_str(), service->index_of.c_str(), service->index_of.size()) == 0)) {
                fc = files->Open(filepath.c_str(), (void*)req.url.c_str());
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
