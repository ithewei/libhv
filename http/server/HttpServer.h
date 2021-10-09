#ifndef HV_HTTP_SERVER_H_
#define HV_HTTP_SERVER_H_

#include "hexport.h"
#include "HttpService.h"
// #include "WebSocketServer.h"
namespace hv {
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
    HttpService* service;
    WebSocketService* ws;
    void* userdata;
//private:
    int listenfd[2]; // 0: http, 1: https
    void* privdata;

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
        service = NULL;
        ws = NULL;
        listenfd[0] = listenfd[1] = -1;
        userdata = NULL;
        privdata = NULL;
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

    HttpServer server;
    server.registerHttpService(&service);
    server.setPort(8080);
    server.setThreadNum(4);
    server.run();
    return 0;
}
*/

namespace hv {

class HttpServer : public http_server_t {
public:
    HttpServer() : http_server_t() {}
    ~HttpServer() { stop(); }

    void registerHttpService(HttpService* service) {
        this->service = service;
    }

    void setHost(const char* host = "0.0.0.0") {
        if (host) strcpy(this->host, host);
    }

    void setPort(int port = 0, int ssl_port = 0) {
        if (port != 0) this->port = port;
        if (ssl_port != 0) this->https_port = ssl_port;
    }

    void setProcessNum(int num) {
        this->worker_processes = num;
    }

    void setThreadNum(int num) {
        this->worker_threads = num;
    }

    int run(bool wait = true) {
        return http_server_run(this, wait);
    }

    int start() {
        return run(false);
    }

    int stop() {
        return http_server_stop(this);
    }
};

}

#endif // HV_HTTP_SERVER_H_
