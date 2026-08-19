/*
 * http_js_handler_test - integration test for HttpJsHandler.
 *
 * Starts a real HttpServer whose JS route awaits hv.sleep(ms). Concurrent
 * requests on a single IO thread must complete faster than serial execution,
 * proving the QuickJS promise continuation is driven by the event loop without
 * blocking it.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <vector>

#include "hbase.h"
#include "hfile.h"
#include "hpath.h"
#include "htime.h"
#include "HttpJsHandler.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "HttpScriptHandler.h"
#include "requests.h"

#define CHECK(expr)                                                                    \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            fprintf(stderr, "CHECK failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static std::string write_script(const char* name, const char* content) {
    hv_mkdir_p("tmp/http_js_handler_test");
    std::string path = HPath::join("tmp/http_js_handler_test", name);
    HFile file;
    int ret = file.open(path.c_str(), "wb");
    CHECK(ret == 0);
    file.write(content, strlen(content));
    file.close();
    return path;
}

int main() {
    std::string script = write_script("sleep.js", "const hv = require('hv');\n"
                                                  "const http = require('hv/http');\n"
                                                  "async function get(ctx) {\n"
                                                  "  await hv.sleep(300);\n"
                                                  "  const resp = await http.get('http://' + ctx.header('Host') + '/ping');\n"
                                                  "  return { ok: true, id: ctx.query('id'), upstream: resp.body };\n"
                                                  "}\n");
    std::string direct_script = write_script("direct.js", "function get(ctx) {\n"
                                                          "  ctx.setHeader('X-From', 'js');\n"
                                                          "  return ctx.text('direct:' + ctx.query('id', ''));\n"
                                                          "}\n");
    std::string circular_script = write_script("circular.js", "function get(ctx) {\n"
                                                              "  const data = { ok: true };\n"
                                                              "  data.self = data;\n"
                                                              "  return data;\n"
                                                              "}\n");

    HttpService service;
    service.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        (void)req;
        resp->body = "pong";
        return 200;
    });
    service.GET("/sleep", hv::HttpScriptHandler(script.c_str()));
    service.GET("/direct", hv::HttpJsHandler(direct_script.c_str()));
    service.GET("/circular", hv::HttpJsHandler(circular_script.c_str()));

    hv::HttpServer server(&service);
    server.setThreadNum(1);
    server.setPort(0);
    CHECK(server.start() == 0);
    CHECK(server.port > 0);
    hv_msleep(200);

    const int N = 5;
    std::vector<std::thread> threads;
    std::atomic<int> ok_count(0);

    uint64_t start = gettimeofday_ms();
    const int server_port = server.port;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([i, server_port, &ok_count]() {
            char url[128];
            snprintf(url, sizeof(url), "http://127.0.0.1:%d/sleep?id=%d", server_port, i);
            auto resp = requests::get(url);
            if (resp == NULL) {
                fprintf(stderr, "request %d failed: null response\n", i);
                return;
            }
            if (resp->status_code != 200) {
                fprintf(stderr, "request %d failed: status=%d body=%s\n", i, resp->status_code, resp->body.c_str());
                return;
            }
            char needle[32];
            snprintf(needle, sizeof(needle), "\"id\":\"%d\"", i);
            if (resp->body.find("\"ok\":true") != std::string::npos && resp->body.find(needle) != std::string::npos &&
                resp->body.find("\"upstream\":\"pong\"") != std::string::npos) {
                ok_count++;
            }
            else {
                fprintf(stderr, "request %d failed: body=%s\n", i, resp->body.c_str());
            }
        });
    }
    for (auto& t : threads) t.join();
    uint64_t elapsed = gettimeofday_ms() - start;

    char direct_url[128];
    snprintf(direct_url, sizeof(direct_url), "http://127.0.0.1:%d/direct?id=7", server_port);
    auto direct_resp = requests::get(direct_url);
    char circular_url[128];
    snprintf(circular_url, sizeof(circular_url), "http://127.0.0.1:%d/circular", server_port);
    auto circular_resp = requests::get(circular_url);

    server.stop();
    hv_msleep(100);

    printf("ok_count=%d/%d elapsed=%llums (each handler awaits 300ms)\n", ok_count.load(), N, (unsigned long long)elapsed);
    CHECK(ok_count.load() == N);
    CHECK(elapsed < 1200);
    CHECK(direct_resp != NULL);
    CHECK(direct_resp->status_code == 200);
    CHECK(direct_resp->body == "direct:7");
    CHECK(direct_resp->GetHeader("X-From") == "js");
    CHECK(circular_resp != NULL);
    CHECK(circular_resp->status_code == 500);
    CHECK(circular_resp->body.find("circular") != std::string::npos);
    printf("ALL http_js_handler_test PASSED\n");
    return 0;
}
