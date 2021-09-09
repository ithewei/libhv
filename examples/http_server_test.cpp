/*
 * sample http server
 * more detail see examples/httpd
 *
 */

#include "HttpServer.h"
#include "hssl.h"

/*
 * #define TEST_HTTPS 1
 *
 * @build   ./configure --with-openssl && make clean && make
 *
 * @server  bin/http_server_test 8080
 *
 * @client  curl -v http://127.0.0.1:8080/ping
 *          curl -v https://127.0.0.1:8443/ping --insecure
 *          bin/curl -v http://127.0.0.1:8080/ping
 *          bin/curl -v https://127.0.0.1:8443/ping
 *
 */
#define TEST_HTTPS 0

int main(int argc, char** argv) {
    HV_MEMCHECK;

    int port = 0;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (port == 0) port = 8080;

    HttpService router;
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    router.GET("/data", [](HttpRequest* req, HttpResponse* resp) {
        static char data[] = "0123456789";
        return resp->Data(data, 10 /*, false */);
    });

    router.GET("/paths", [&router](HttpRequest* req, HttpResponse* resp) {
        return resp->Json(router.Paths());
    });

    router.GET("/get", [](HttpRequest* req, HttpResponse* resp) {
        resp->json["origin"] = req->client_addr.ip;
        resp->json["url"] = req->url;
        resp->json["args"] = req->query_params;
        resp->json["headers"] = req->headers;
        return 200;
    });

    router.POST("/echo", [](const HttpContextPtr& ctx) {
        return ctx->send(ctx->body(), ctx->type());
    });

    http_server_t server;
    server.service = &router;
    server.port = port;
#if TEST_HTTPS
    server.https_port = 8443;
    hssl_ctx_init_param_t param;
    memset(&param, 0, sizeof(param));
    param.crt_file = "cert/server.crt";
    param.key_file = "cert/server.key";
    param.endpoint = HSSL_SERVER;
    if (hssl_ctx_init(&param) == NULL) {
        fprintf(stderr, "hssl_ctx_init failed!\n");
        return -20;
    }
#endif

    // uncomment to test multi-processes
    // server.worker_processes = 4;
    // uncomment to test multi-threads
    // server.worker_threads = 4;

#if 1
    http_server_run(&server);
#else
    // test http_server_stop
    http_server_run(&server, 0);
    hv_sleep(10);
    http_server_stop(&server);
#endif
    return 0;
}
