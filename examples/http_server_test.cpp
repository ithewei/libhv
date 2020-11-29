#include "HttpServer.h"

int main() {
    HttpService service;
    service.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        resp->body = "pong";
        return 200;
    });

    service.POST("/echo", [](HttpRequest* req, HttpResponse* resp) {
        resp->content_type = req->content_type;
        resp->body = req->body;
        return 200;
    });

    http_server_t server;
    server.port = 8080;
    server.service = &service;
    http_server_run(&server);
    return 0;
}
