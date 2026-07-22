/*
 * asynchttp_dns_test — integration test for async DNS in AsyncHttpClient.
 *
 * Verifies that an async HTTP request to a *hostname* URL (not a numeric IP)
 * resolves the hostname through hdns (async, non-blocking) and completes.
 *
 * Uses "http://localhost:PORT/ping" against a local HttpServer, so it needs no
 * external network (localhost resolves via /etc/hosts inside hdns).
 */

#include <atomic>
#include <cstdio>

#include "HttpServer.h"
#include "HttpClient.h"     // http_client_send_async
#include "htime.h"

using namespace hv;

int main() {
    // 1. start a local HTTP server on a fixed port
    int port = 10880;
    HttpService router;
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    HttpServer server;
    server.registerHttpService(&router);
    server.setPort(port);
    server.setThreadNum(1);
    if (server.start() != 0) {
        printf("server start failed on port %d\n", port);
        return 1;
    }
    printf("http server started on localhost:%d\n", port);
    hv_msleep(200);

    // 2. issue an async HTTP request to the *hostname* URL
    std::atomic<bool> done{false};
    std::atomic<int> status_code{0};
    std::string body;

    auto req = std::make_shared<HttpRequest>();
    char url[128];
    snprintf(url, sizeof(url), "http://localhost:%d/ping", port);
    req->url = url;              // hostname "localhost" -> exercises async DNS
    req->method = HTTP_GET;
    req->timeout = 5;

    http_client_send_async(req, [&](const HttpResponsePtr& resp) {
        if (resp) {
            status_code = resp->status_code;
            body = resp->body;
        }
        done = true;
    });

    // 3. wait for completion
    uint64_t start = gettick_ms();
    while (!done && gettick_ms() - start < 6000) {
        hv_msleep(20);
    }

    server.stop();
    hv_msleep(100);

    if (done && status_code == 200 && body == "pong") {
        printf("\nPASS: async HTTP request to hostname resolved via hdns "
               "(status=%d body=%s)\n", status_code.load(), body.c_str());
        return 0;
    }
    printf("\nFAIL: done=%d status=%d body=%s\n",
           (int)done, status_code.load(), body.c_str());
    return 2;
}
