#ifndef HV_HTTP_SERVICE_H_
#define HV_HTTP_SERVICE_H_

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
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

// for FileCache
#define MAX_FILE_CACHE_SIZE                 (1 << 22)   // 4M
#define DEFAULT_FILE_CACHE_STAT_INTERVAL    10          // s
#define DEFAULT_FILE_CACHE_EXPIRED_TIME     60          // s

/*
 * @param[in]  req:  parsed structured http request
 * @param[out] resp: structured http response
 * @return  0:                  handle next
 *          http_status_code:   handle done
 */
#define HTTP_STATUS_NEXT        0
#define HTTP_STATUS_UNFINISHED  0
// NOTE: http_sync_handler run on IO thread
typedef std::function<int(HttpRequest* req, HttpResponse* resp)>                            http_sync_handler;
// NOTE: http_async_handler run on hv::async threadpool
typedef std::function<void(const HttpRequestPtr& req, const HttpResponseWriterPtr& writer)> http_async_handler;
// NOTE: http_ctx_handler run on IO thread, you can easily post HttpContextPtr to your consumer thread for processing.
typedef std::function<int(const HttpContextPtr& ctx)>                                       http_ctx_handler;
// NOTE: http_state_handler run on IO thread
typedef std::function<int(const HttpContextPtr& ctx, http_parser_state state, const char* data, size_t size)> http_state_handler;

struct http_handler {
    http_sync_handler   sync_handler;
    http_async_handler  async_handler;
    http_ctx_handler    ctx_handler;
    http_state_handler  state_handler;

    http_handler()  {}
    http_handler(http_sync_handler fn)  : sync_handler(std::move(fn))   {}
    http_handler(http_async_handler fn) : async_handler(std::move(fn))  {}
    http_handler(http_ctx_handler fn)   : ctx_handler(std::move(fn))    {}
    http_handler(http_state_handler fn) : state_handler(std::move(fn))  {}
    http_handler(const http_handler& rhs)
        : sync_handler(std::move(const_cast<http_handler&>(rhs).sync_handler))
        , async_handler(std::move(const_cast<http_handler&>(rhs).async_handler))
        , ctx_handler(std::move(const_cast<http_handler&>(rhs).ctx_handler))
        , state_handler(std::move(const_cast<http_handler&>(rhs).state_handler))
    {}

    const http_handler& operator=(http_sync_handler fn) {
        sync_handler = std::move(fn);
        return *this;
    }
    const http_handler& operator=(http_async_handler fn) {
        async_handler = std::move(fn);
        return *this;
    }
    const http_handler& operator=(http_ctx_handler fn) {
        ctx_handler = std::move(fn);
        return *this;
    }
    const http_handler& operator=(http_state_handler fn) {
        state_handler = std::move(fn);
        return *this;
    }

    bool isNull() {
        return  sync_handler == NULL &&
                async_handler == NULL &&
                ctx_handler == NULL;
    }

    operator bool() {
        return !isNull();
    }
};

typedef std::vector<http_handler>   http_handlers;

struct http_method_handler {
    http_method         method;
    http_handler        handler;

    http_method_handler()   {}
    http_method_handler(http_method m, const http_handler& h) : method(m), handler(h) {}
};

// method => http_method_handler
typedef std::list<http_method_handler>                                          http_method_handlers;
// path   => http_method_handlers
typedef std::unordered_map<std::string, std::shared_ptr<http_method_handlers>>  http_path_handlers;

namespace hv {

struct HV_EXPORT HttpService {
    /* handler chain */
    // headerHandler -> preprocessor -> middleware -> processor -> postprocessor
    http_handler        headerHandler;
    http_handler        preprocessor;
    http_handlers       middleware;
    // processor: pathHandlers -> staticHandler -> errorHandler
    http_handler        processor;
    http_handler        postprocessor;

    /* API handlers */
    std::string         base_url;
    http_path_handlers  pathHandlers;

    /* Static file service */
    http_handler    staticHandler;
    http_handler    largeFileHandler;
    std::string     document_root;
    std::string     home_page;
    std::string     error_page;
    // nginx: location => root
    std::map<std::string, std::string, std::greater<std::string>> staticDirs;
    /* Indexof directory service */
    std::string     index_of;
    http_handler    errorHandler;

    /* Proxy service */
    /* Reverse proxy service */
    // nginx: location => proxy_pass
    std::map<std::string, std::string, std::greater<std::string>> proxies;
    /* Forward proxy service */
    StringList  trustProxies;
    StringList  noProxies;
    int proxy_connect_timeout;
    int proxy_read_timeout;
    int proxy_write_timeout;

    // options
    int keepalive_timeout;
    int max_file_cache_size;        // cache small file
    int file_cache_stat_interval;   // stat file is modified
    int file_cache_expired_time;    // remove expired file cache
    /*
     * @test    limit_rate
     * @build   make examples
     * @server  bin/httpd -c etc/httpd.conf -s restart -d
     * @client  bin/wget http://127.0.0.1:8080/downloads/test.zip
     */
    int limit_rate; // limit send rate, unit: KB/s

    unsigned enable_access_log      :1;
    unsigned enable_forward_proxy   :1;

    HttpService() {
        // base_url = DEFAULT_BASE_URL;

        document_root = DEFAULT_DOCUMENT_ROOT;
        home_page = DEFAULT_HOME_PAGE;
        // error_page = DEFAULT_ERROR_PAGE;
        // index_of = DEFAULT_INDEXOF_DIR;

        proxy_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
        proxy_read_timeout = 0;
        proxy_write_timeout = 0;

        keepalive_timeout = DEFAULT_KEEPALIVE_TIMEOUT;
        max_file_cache_size = MAX_FILE_CACHE_SIZE;
        file_cache_stat_interval = DEFAULT_FILE_CACHE_STAT_INTERVAL;
        file_cache_expired_time = DEFAULT_FILE_CACHE_EXPIRED_TIME;
        limit_rate = -1; // unlimited

        enable_access_log = 1;
        enable_forward_proxy = 0;
    }

    void AddRoute(const char* path, http_method method, const http_handler& handler);
    // @retval 0 OK, else HTTP_STATUS_NOT_FOUND, HTTP_STATUS_METHOD_NOT_ALLOWED
    int  GetRoute(const char* url,  http_method method, http_handler** handler);
    // RESTful API /:field/ => req->query_params["field"]
    int  GetRoute(HttpRequest* req, http_handler** handler);

    // Static("/", "/var/www/html")
    void Static(const char* path, const char* dir);
    // @retval / => /var/www/html/index.html
    std::string GetStaticFilepath(const char* path);

    // https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
    void AllowCORS();

    // proxy
    // forward proxy
    void EnableForwardProxy() { enable_forward_proxy = 1; }
    void AddTrustProxy(const char* host);
    void AddNoProxy(const char* host);
    bool IsTrustProxy(const char* host);
    // reverse proxy
    // Proxy("/api/v1/", "http://www.httpbin.org/");
    void Proxy(const char* path, const char* url);
    // @retval /api/v1/test => http://www.httpbin.org/test
    std::string GetProxyUrl(const char* path);

    hv::StringList Paths() {
        hv::StringList paths;
        for (auto& pair : pathHandlers) {
            paths.emplace_back(pair.first);
        }
        return paths;
    }

    // Handler = [ http_sync_handler, http_ctx_handler ]
    template<typename Handler>
    void Use(Handler handlerFunc) {
        middleware.emplace_back(handlerFunc);
    }

    // Inspired by github.com/gin-gonic/gin
    // Handler = [ http_sync_handler, http_async_handler, http_ctx_handler, http_state_handler ]
    template<typename Handler>
    void Handle(const char* httpMethod, const char* relativePath, Handler handlerFunc) {
        AddRoute(relativePath, http_method_enum(httpMethod), http_handler(handlerFunc));
    }

    // HEAD
    template<typename Handler>
    void HEAD(const char* relativePath, Handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
    }

    // GET
    template<typename Handler>
    void GET(const char* relativePath, Handler handlerFunc) {
        Handle("GET", relativePath, handlerFunc);
    }

    // POST
    template<typename Handler>
    void POST(const char* relativePath, Handler handlerFunc) {
        Handle("POST", relativePath, handlerFunc);
    }

    // PUT
    template<typename Handler>
    void PUT(const char* relativePath, Handler handlerFunc) {
        Handle("PUT", relativePath, handlerFunc);
    }

    // DELETE
    // NOTE: Windows <winnt.h> #define DELETE as a macro, we have to replace DELETE with Delete.
    template<typename Handler>
    void Delete(const char* relativePath, Handler handlerFunc) {
        Handle("DELETE", relativePath, handlerFunc);
    }

    // PATCH
    template<typename Handler>
    void PATCH(const char* relativePath, Handler handlerFunc) {
        Handle("PATCH", relativePath, handlerFunc);
    }

    // Any
    template<typename Handler>
    void Any(const char* relativePath, Handler handlerFunc) {
        Handle("HEAD", relativePath, handlerFunc);
        Handle("GET", relativePath, handlerFunc);
        Handle("POST", relativePath, handlerFunc);
        Handle("PUT", relativePath, handlerFunc);
        Handle("DELETE", relativePath, handlerFunc);
        Handle("PATCH", relativePath, handlerFunc);
    }
};

}

#endif // HV_HTTP_SERVICE_H_
