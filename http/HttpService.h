#ifndef HTTP_SERVICE_H_
#define HTTP_SERVICE_H_

#include <string.h>
#include <string>
#include <map>
#include <list>
#include <memory>

#include "HttpRequest.h"

#define DEFAULT_BASE_URL        "/v1/api"
#define DEFAULT_DOCUMENT_ROOT   "/var/www/html"
#define DEFAULT_HOME_PAGE       "index.html"

typedef void (*http_api_handler)(HttpRequest* req, HttpResponse* res);

struct http_method_handler {
    http_method         method;
    http_api_handler    handler;
    http_method_handler(http_method m = HTTP_POST, http_api_handler h = NULL) {
        method = m;
        handler = h;
    }
};
// Provide Restful API
typedef std::list<http_method_handler> http_method_handlers;
// path => http_method_handlers
typedef std::map<std::string, std::shared_ptr<http_method_handlers>> http_api_handlers;

struct HttpService {
    http_api_handler    preprocessor;
    http_api_handler    postprocessor;
    // api service
    std::string         base_url;
    http_api_handlers   api_handlers;
    // web service
    std::string document_root;
    std::string home_page;
    std::string error_page;

    HttpService() {
        preprocessor = NULL;
        postprocessor = NULL;
        base_url = DEFAULT_BASE_URL;
        document_root = DEFAULT_DOCUMENT_ROOT;
        home_page = DEFAULT_HOME_PAGE;
    }

    void AddApi(const char* path, http_method method, http_api_handler handler) {
        std::shared_ptr<http_method_handlers> method_handlers = NULL;
        auto iter = api_handlers.find(path);
        if (iter == api_handlers.end()) {
            // add path
            method_handlers = std::shared_ptr<http_method_handlers>(new http_method_handlers);
            api_handlers[path] = method_handlers;
        }
        else {
            method_handlers = iter->second;
        }
        for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
            if (iter->method == method) {
                // update
                iter->handler = handler;
                return;
            }
        }
        // add
        method_handlers->push_back(http_method_handler(method, handler));
    }

    int GetApi(const char* url, http_method method, http_api_handler* handler) {
        // {base_url}/path?query
        const char* s = url;
        const char* c = base_url.c_str();
        while (*s != '\0' && *c != '\0' && *s == *c) {++s;++c;}
        if (*c != '\0') {
            return HTTP_STATUS_NOT_FOUND;
        }
        const char* e = s;
        while (*e != '\0' && *e != '?') ++e;

        std::string path = std::string(s, e);
        auto iter = api_handlers.find(path);
        if (iter == api_handlers.end()) {
            *handler = NULL;
            return HTTP_STATUS_NOT_FOUND;
        }
        auto method_handlers = iter->second;
        for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
            if (iter->method == method) {
                *handler = iter->handler;
                return 0;
            }
        }
        *handler = NULL;
        return HTTP_STATUS_METHOD_NOT_ALLOWED;
    }
};

#endif // HTTP_SERVICE_H_

