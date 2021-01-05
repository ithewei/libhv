/*
 * sample http server
 * more detail see examples/httpd
 *
 */

#include "HttpServer.h"

int main() {
    HV_MEMCHECK;

    HttpService router;
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    router.GET("/data", [](HttpRequest* req, HttpResponse* resp) {
        static char data[] = "0123456789";
        return resp->Data(data, 10);
    });

    router.GET("/paths", [&router](HttpRequest* req, HttpResponse* resp) {
        return resp->Json(router.Paths());
    });

    router.POST("/echo", [](HttpRequest* req, HttpResponse* resp) {
        resp->content_type = req->content_type;
        resp->body = req->body;
        return 200;
    });

    http_server_t server;
    server.port = 8080;
    // uncomment to test multi-processes
    // server.worker_processes = 4;
    // uncomment to test multi-threads
    // server.worker_threads = 4;
    server.service = &router;

#if 1
    http_server_run(&server);
#else
    // test http_server_stop
    http_server_run(&server, 0);
    sleep(10);
    http_server_stop(&server);
#endif
    return 0;
}
