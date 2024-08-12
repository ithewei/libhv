#ifndef HV_HTTP_SERVER_H_
#define HV_HTTP_SERVER_H_

#include "hexport.h"
#include "hssl.h"
// #include "EventLoop.h"
#include "HttpService.h"
// #include "WebSocketServer.h"
namespace hv {
class EventLoop;
struct WebSocketService;
}
using hv::HttpService;
using hv::WebSocketService;

typedef struct http_server_s {
    char host[64];
    int port; // http_port
    int https_port;
    int http_version;
    int worker_processes;
    int worker_threads;
    uint32_t worker_connections; // max_connections = workers * worker_connections
    HttpService* service; // http service
    WebSocketService* ws; // websocket service
    void* userdata;
    int listenfd[2]; // 0: http, 1: https
    void* privdata;
    // hooks
    std::function<void()> onWorkerStart;
    std::function<void()> onWorkerStop;
    // SSL/TLS
    hssl_ctx_t  ssl_ctx;
    unsigned    alloced_ssl_ctx: 1;

#ifdef __cplusplus
    http_server_s() {
        strcpy(host, "0.0.0.0");
        // port = DEFAULT_HTTP_PORT;
        // https_port = DEFAULT_HTTPS_PORT;
        // port = 8080;
        // https_port = 8443;
        port = https_port = 0;
        http_version = 1;
        worker_processes = 0;
        worker_threads = 0;
        worker_connections = 1024;
        service = NULL;
        ws = NULL;
        listenfd[0] = listenfd[1] = -1;
        userdata = NULL;
        privdata = NULL;
        // SSL/TLS
        ssl_ctx = NULL;
        alloced_ssl_ctx = 0;
    }
#endif
} http_server_t;

// @param wait: Whether to occupy current thread
HV_EXPORT int http_server_run(http_server_t* server, int wait = 1);

// NOTE: stop all loops and join all threads
HV_EXPORT int http_server_stop(http_server_t* server);

/*
#include "HttpServer.h"
using namespace hv;

int main() {
    HttpService service;
    service.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        resp->body = "pong";
        return 200;
    });

    HttpServer server(&service);
    server.setThreadNum(4);
    server.run(":8080");
    return 0;
}
*/

namespace hv {

class HV_EXPORT HttpServer : public http_server_t {
public:
    HttpServer(HttpService* service = NULL)
        : http_server_t()
    {
        this->service = service;
    }
    ~HttpServer() { stop(); }

    void registerHttpService(HttpService* service) {
        this->service = service;
    }

    std::shared_ptr<hv::EventLoop> loop(int idx = -1);

    void setHost(const char* host = "0.0.0.0") {
        if (host) strcpy(this->host, host);
    }

    void setPort(int port = 0, int ssl_port = 0) {
        if (port >= 0) this->port = port;
        if (ssl_port >= 0) this->https_port = ssl_port;
    }
    void setListenFD(int fd = -1, int ssl_fd = -1) {
        if (fd >= 0) this->listenfd[0] = fd;
        if (ssl_fd >= 0) this->listenfd[1] = ssl_fd;
    }

    void setProcessNum(int num) {
        this->worker_processes = num;
    }

    void setThreadNum(int num) {
        this->worker_threads = num;
    }

    void setMaxWorkerConnectionNum(uint32_t num) {
        this->worker_connections = num;
    }
    size_t connectionNum();

    // SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx) {
        this->ssl_ctx = ssl_ctx;
        return 0;
    }
    int newSslCtx(hssl_ctx_opt_t* opt) {
        // NOTE: hssl_ctx_free in http_server_stop
        hssl_ctx_t ssl_ctx = hssl_ctx_new(opt);
        if (ssl_ctx == NULL) return -1;
        this->alloced_ssl_ctx = 1;
        return setSslCtx(ssl_ctx);
    }

    // run(":8080")
    // run("0.0.0.0:8080")
    // run("[::]:8080")
    int run(const char* ip_port = NULL, bool wait = true) {
        if (ip_port) {
            hv::NetAddr listen_addr(ip_port);
            if (listen_addr.ip.size() != 0) setHost(listen_addr.ip.c_str());
            if (listen_addr.port != 0)      setPort(listen_addr.port);
        }
        return http_server_run(this, wait);
    }

    int start(const char* ip_port = NULL) {
        return run(ip_port, false);
    }

    int stop() {
        return http_server_stop(this);
    }
};

}

#endif // HV_HTTP_SERVER_H_
