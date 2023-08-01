/*
 * sample http server
 * more detail see examples/httpd
 *
 */

#include "HttpServer.h"
#include "hthread.h"    // import hv_gettid
#include "hasync.h"     // import hv::async

using namespace hv;

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

    /* Static file service */
    // curl -v http://ip:port/
    router.Static("/", "./html");

    /* Forward proxy service */
    router.EnableForwardProxy();
    // curl -v http://httpbin.org/get --proxy http://127.0.0.1:8080
    router.AddTrustProxy("*httpbin.org");

    /* Reverse proxy service */
    // curl -v http://ip:port/httpbin/get
    router.Proxy("/httpbin/", "http://httpbin.org/");

    /* API handlers */
    // curl -v http://ip:port/ping
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    // curl -v http://ip:port/data
    router.GET("/data", [](HttpRequest* req, HttpResponse* resp) {
        static char data[] = "0123456789";
        return resp->Data(data, 10 /*, false */);
    });

    // curl -v http://ip:port/paths
    router.GET("/paths", [&router](HttpRequest* req, HttpResponse* resp) {
        return resp->Json(router.Paths());
    });

    // curl -v http://ip:port/get?env=1
    router.GET("/get", [](const HttpContextPtr& ctx) {
        hv::Json resp;
        resp["origin"] = ctx->ip();
        resp["url"] = ctx->url();
        resp["args"] = ctx->params();
        resp["headers"] = ctx->headers();
        return ctx->send(resp.dump(2));
    });

    // curl -v http://ip:port/echo -d "hello,world!"
    router.POST("/echo", [](const HttpContextPtr& ctx) {
        return ctx->send(ctx->body(), ctx->type());
    });

    // curl -v http://ip:port/user/123
    router.GET("/user/{id}", [](const HttpContextPtr& ctx) {
        hv::Json resp;
        resp["id"] = ctx->param("id");
        return ctx->send(resp.dump(2));
    });

    // curl -v http://ip:port/async
    router.GET("/async", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        writer->Begin();
        writer->WriteHeader("X-Response-tid", hv_gettid());
        writer->WriteHeader("Content-Type", "text/plain");
        writer->WriteBody("This is an async response.\n");
        writer->End();
    });

    // middleware
    router.AllowCORS();
    router.Use([](HttpRequest* req, HttpResponse* resp) {
        resp->SetHeader("X-Request-tid", hv::to_string(hv_gettid()));
        return HTTP_STATUS_NEXT;
    });

    HttpServer server;
    server.service = &router;
    server.port = port;
#if TEST_HTTPS
    server.https_port = 8443;
    hssl_ctx_opt_t param;
    memset(&param, 0, sizeof(param));
    param.crt_file = "cert/server.crt";
    param.key_file = "cert/server.key";
    param.endpoint = HSSL_SERVER;
    if (server.newSslCtx(&param) != 0) {
        fprintf(stderr, "new SSL_CTX failed!\n");
        return -20;
    }
#endif

    // uncomment to test multi-processes
    // server.setProcessNum(4);
    // uncomment to test multi-threads
    // server.setThreadNum(4);

    server.start();

    // press Enter to stop
    while (getchar() != '\n');
    hv::async::cleanup();
    return 0;
}
