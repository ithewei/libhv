#ifndef HV_HTTP_SERVICE_H_
#define HV_HTTP_SERVICE_H_

#include <string>
#include <map>
#include <list>
#include <memory>
#include <functional>

#include "hexport.h"
#include "HttpMessage.h"
#include "HttpResponseWriter.h"
#include "HttpContext.h"

#define DEFAULT_BASE_URL        "/api/v1"
#define DEFAULT_DOCUMENT_ROOT   "/var/www/html"
#define DEFAULT_HOME_PAGE       "index.html"
#define DEFAULT_ERROR_PAGE      "error.html"
#define DEFAULT_INDEXOF_DIR     "/downloads/"
#define DEFAULT_KEEPALIVE_TIMEOUT   75000   // ms

/*
 * @param[in]  req:  parsed structured http request
 * @param[out] resp: structured http response
 * @return  0:                  handle unfinished
 *          http_status_code:   handle done
 */
#define HTTP_STATUS_UNFINISHED  0
typedef std::function<int(HttpRequest* req, HttpResponse* resp)>                            http_sync_handler;
typedef std::function<void(const HttpRequestPtr& req, const HttpResponseWriterPtr& writer)> http_async_handler;
typedef std::function<int(const HttpContextPtr& ctx)>                                       http_handler;

struct http_method_handler {
    http_method         method;
    http_sync_handler   sync_handler;
    http_async_handler  async_handler;
    http_handler        handler;
    http_method_handler(http_method m = HTTP_POST,
                        http_sync_handler s = NULL,
                        http_async_handler a = NULL,
                        http_handler h = NULL)
    {
        method = m;
        sync_handler = std::move(s);
        async_handler = std::move(a);
        handler = std::move(h);
    }
};
// method => http_method_handler
typedef std::list<http_method_handler>                                  http_method_handlers;
// path => http_method_handlers
typedef std::map<std::string, std::shared_ptr<http_method_handlers>>    http_api_handlers;

struct HV_EXPORT HttpService {
    // preprocessor -> processor -> postprocessor
    http_sync_handler   preprocessor;
    // processor: api_handlers -> staticHandler -> errorHandler
    http_handler        processor;
    http_sync_handler   postprocessor;

    // api service (that is http.APIServer)
    std::string         base_url;
    http_api_handlers   api_handlers;

    // file service (that is http.FileServer)
    http_handler    staticHandler;
    http_handler    largeFileHandler;
    std::string     document_root;
    std::string     home_page;
    std::string     error_page;
    // indexof service (that is http.DirectoryServer)
    std::string     index_of;

    http_handler    errorHandler;

    // options
    int keepalive_timeout;

    HttpService() {
        preprocessor = NULL;
        processor = NULL;
        postprocessor = NULL;

        // base_url = DEFAULT_BASE_URL;

        staticHandler = NULL;
        largeFileHandler = NULL;

        document_root = DEFAULT_DOCUMENT_ROOT;
        home_page = DEFAULT_HOME_PAGE;
        // error_page = DEFAULT_ERROR_PAGE;
        // index_of = DEFAULT_INDEXOF_DIR;

        errorHandler = NULL;

        keepalive_timeout = DEFAULT_KEEPALIVE_TIMEOUT;
    }

    void AddApi(const char* path, http_method method,
            http_sync_handler sync_handler = NULL,
            http_async_handler async_handler = NULL,
            http_handler handler = NULL);
    // @retval 0 OK, else HTTP_STATUS_NOT_FOUND, HTTP_STATUS_METHOD_NOT_ALLOWED
    int GetApi(const char* url, http_method method,
            http_sync_handler* sync_handler = NULL,
            http_async_handler* async_handler = NULL,
            http_handler* handler = NULL);
    // RESTful API /:field/ => req->query_params["field"]
    int GetApi(HttpRequest* req,
            http_sync_handler* sync_handler = NULL,
            http_async_handler* async_handler = NULL,
            http_handler* handler = NULL);

    StringList Paths() {
        StringList paths;
        for (auto& pair : api_handlers) {
            paths.emplace_back(pair.first);
        }
        return paths;
    }

    // github.com/gin-gonic/gin
    void Handle(const char* httpMethod, const char* relativePath, http_sync_handler handlerFunc) {
        AddApi(relativePath, http_method_enum(httpMethod), handlerFunc, NULL, NULL);
    }
    void Handle(const char* httpMethod, const char* relativePath, http_async_handler handlerFunc) {
        AddApi(relativePath, http_method_enum(httpMethod), NULL, handlerFunc, NULL);
    }
    void Handle(const char* httpMethod, const char* relativePath, http_handler handlerFunc) {
        AddApi(relativePath, http_method_enum(httpMethod), NULL, NULL, handlerFunc);
    }

    // HEAD
    void HEAD(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
    }
    void HEAD(const char* relativePath, http_async_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
    }
    void HEAD(const char* relativePath, http_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
    }

    // GET
    void GET(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("GET", relativePath, handlerFunc);
    }
    void GET(const char* relativePath, http_async_handler handlerFunc) {
        Handle("GET", relativePath, handlerFunc);
    }
    void GET(const char* relativePath, http_handler handlerFunc) {
        Handle("GET", relativePath, handlerFunc);
    }

    // POST
    void POST(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("POST", relativePath, handlerFunc);
    }
    void POST(const char* relativePath, http_async_handler handlerFunc) {
        Handle("POST", relativePath, handlerFunc);
    }
    void POST(const char* relativePath, http_handler handlerFunc) {
        Handle("POST", relativePath, handlerFunc);
    }

    // PUT
    void PUT(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("PUT", relativePath, handlerFunc);
    }
    void PUT(const char* relativePath, http_async_handler handlerFunc) {
        Handle("PUT", relativePath, handlerFunc);
    }
    void PUT(const char* relativePath, http_handler handlerFunc) {
        Handle("PUT", relativePath, handlerFunc);
    }

    // DELETE
    // NOTE: Windows <winnt.h> #define DELETE as a macro, we have to replace DELETE with Delete.
    void Delete(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("DELETE", relativePath, handlerFunc);
    }
    void Delete(const char* relativePath, http_async_handler handlerFunc) {
        Handle("DELETE", relativePath, handlerFunc);
    }
    void Delete(const char* relativePath, http_handler handlerFunc) {
        Handle("DELETE", relativePath, handlerFunc);
    }

    // PATCH
    void PATCH(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("PATCH", relativePath, handlerFunc);
    }
    void PATCH(const char* relativePath, http_async_handler handlerFunc) {
        Handle("PATCH", relativePath, handlerFunc);
    }
    void PATCH(const char* relativePath, http_handler handlerFunc) {
        Handle("PATCH", relativePath, handlerFunc);
    }

    // Any
    void Any(const char* relativePath, http_sync_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
        Handle("GET", relativePath, handlerFunc);
        Handle("POST", relativePath, handlerFunc);
        Handle("PUT", relativePath, handlerFunc);
        Handle("DELETE", relativePath, handlerFunc);
        Handle("PATCH", relativePath, handlerFunc);
    }
    void Any(const char* relativePath, http_async_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
        Handle("GET", relativePath, handlerFunc);
        Handle("POST", relativePath, handlerFunc);
        Handle("PUT", relativePath, handlerFunc);
        Handle("DELETE", relativePath, handlerFunc);
        Handle("PATCH", relativePath, handlerFunc);
    }
    void Any(const char* relativePath, http_handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
        Handle("GET", relativePath, handlerFunc);
        Handle("POST", relativePath, handlerFunc);
        Handle("PUT", relativePath, handlerFunc);
        Handle("DELETE", relativePath, handlerFunc);
        Handle("PATCH", relativePath, handlerFunc);
    }
};

#endif // HV_HTTP_SERVICE_H_
