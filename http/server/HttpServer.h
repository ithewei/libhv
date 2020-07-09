#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include "hexport.h"
#include "HttpService.h"

typedef struct http_server_s {
    char host[64];
    int port;
    int ssl;
    int http_version;
    HttpService* service;
    int worker_processes;
    int worker_threads;
    void* userdata;
//private:
    int listenfd;
    void* privdata;

#ifdef __cplusplus
    http_server_s() {
        strcpy(host, "0.0.0.0");
        port = DEFAULT_HTTP_PORT;
        ssl = 0;
        http_version = 1;
        service = NULL;
        worker_processes = 0;
        worker_threads = 0;
        listenfd = -1;
        userdata = NULL;
        privdata = NULL;
    }
#endif
} http_server_t;

/*
#include "HttpServer.h"

int http_api_hello(HttpRequest* req, HttpResponse* res) {
    res->body = "hello";
    return 0;
}

int main() {
    HttpService service;
    service.base_url = "/v1/api";
    service.AddApi("/hello", HTTP_GET, http_api_hello);

    http_server_t server;
    server.port = 8080;
    server.worker_processes = 4;
    server.service = &service;
    http_server_run(&server);
    return 0;
}
 */
HV_EXPORT int http_server_run(http_server_t* server, int wait = 1);

// just for worker_processes = 0
HV_EXPORT int http_server_stop(http_server_t* server);

#endif
