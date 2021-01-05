#ifndef HTTP_SERVICE_H_
#define HTTP_SERVICE_H_

#include <string>
#include <map>
#include <list>
#include <memory>
#include <functional>

#include "hexport.h"
#include "HttpMessage.h"

#define DEFAULT_BASE_URL        "/v1/api"
#define DEFAULT_DOCUMENT_ROOT   "/var/www/html"
#define DEFAULT_HOME_PAGE       "index.html"

/*
 * @param[in] req: parsed structured http request
 * @param[out] res: structured http response
 * @return  0: handle continue
 *          http_status_code: handle done
 */
// typedef int (*http_api_handler)(HttpRequest* req, HttpResponse* res);
// NOTE: use std::function/std::bind is more convenient and more flexible.
typedef std::function<int(HttpRequest* req, HttpResponse* resp)> http_api_handler;

struct http_method_handler {
    http_method         method;
    http_api_handler    handler;
    http_method_handler(http_method m = HTTP_POST, http_api_handler h = NULL) {
        method = m;
        handler = h;
    }
};
// method => http_api_handler
typedef std::list<http_method_handler> http_method_handlers;
// path => http_method_handlers
typedef std::map<std::string, std::shared_ptr<http_method_handlers>> http_api_handlers;

struct HV_EXPORT HttpService {
    // preprocessor -> api -> web -> postprocessor
    http_api_handler    preprocessor;
    http_api_handler    postprocessor;
    // api service (that is http.APIServer)
    std::string         base_url;
    http_api_handlers   api_handlers;
    // web service (that is http.FileServer)
    std::string document_root;
    std::string home_page;
    std::string error_page;
    // indexof service (that is http.DirectoryServer)
    std::string index_of;

    HttpService() {
        preprocessor = NULL;
        postprocessor = NULL;
        // base_url = DEFAULT_BASE_URL;
        document_root = DEFAULT_DOCUMENT_ROOT;
        home_page = DEFAULT_HOME_PAGE;
    }

    void AddApi(const char* path, http_method method, http_api_handler handler);
    // @retval 0 OK, else HTTP_STATUS_NOT_FOUND, HTTP_STATUS_METHOD_NOT_ALLOWED
    int GetApi(const char* url, http_method method, http_api_handler* handler);
    // RESTful API /:field/ => req->query_params["field"]
    int GetApi(HttpRequest* req, http_api_handler* handler);

    StringList Paths() {
        StringList paths;
        for (auto& pair : api_handlers) {
            paths.emplace_back(pair.first);
        }
        return paths;
    }

    // github.com/gin-gonic/gin
    void Handle(const char* httpMethod, const char* relativePath, http_api_handler handlerFunc) {
        AddApi(relativePath, http_method_enum(httpMethod), handlerFunc);
    }

    void HEAD(const char* relativePath, http_api_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
    }

    void GET(const char* relativePath, http_api_handler handlerFunc) {
        Handle("GET", relativePath, handlerFunc);
    }

    void POST(const char* relativePath, http_api_handler handlerFunc) {
        Handle("POST", relativePath, handlerFunc);
    }

    void PUT(const char* relativePath, http_api_handler handlerFunc) {
        Handle("PUT", relativePath, handlerFunc);
    }

    // NOTE: Windows <winnt.h> #define DELETE as a macro, we have to replace DELETE with Delete.
    void Delete(const char* relativePath, http_api_handler handlerFunc) {
        Handle("DELETE", relativePath, handlerFunc);
    }

    void PATCH(const char* relativePath, http_api_handler handlerFunc) {
        Handle("PATCH", relativePath, handlerFunc);
    }

    void Any(const char* relativePath, http_api_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
        Handle("GET", relativePath, handlerFunc);
        Handle("POST", relativePath, handlerFunc);
        Handle("PUT", relativePath, handlerFunc);
        Handle("DELETE", relativePath, handlerFunc);
        Handle("PATCH", relativePath, handlerFunc);
    }
};

#endif // HTTP_SERVICE_H_
