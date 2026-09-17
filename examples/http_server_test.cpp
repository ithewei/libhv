/*
 * sample http server
 * more detail see examples/httpd
 *
 */

#include "HttpServer.h"
#include "hthread.h"    // import hv_gettid
#include "htime.h"      // import gettimeofday_ms
#include "hasync.h"     // import hv::async
#include "EventLoop.h"  // import hv::setInterval

#if defined(WITH_LUA) || defined(WITH_JS)
#include "HttpScriptHandler.h"
#endif

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

// NOTE: single-process (multi-threaded) server, so the HttpServerStat counters
// aggregate across all worker threads. Under multi-process mode the counters
// would be per-process; see the /stats note below.
static HttpServer g_server;

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

    // curl -v http://ip:port/stats
    // Runtime server stats as JSON: current/total connections, completed
    // requests, received/sent bytes, plus per-second rates refreshed by a
    // 60s interval timer (diff of two HttpServerStat snapshots divided by
    // the elapsed seconds).
    // NOTE: counters are per-process; this demo runs single-process so they
    // aggregate all worker threads. Under multi-process mode each process has
    // its own counters and an external collector must aggregate them.
    router.GET("/stats", [](HttpRequest* req, HttpResponse* resp) {
        // published per-second rates (written by the timer, read here)
        static std::atomic<double> conn_per_sec{0};
        static std::atomic<double> req_per_sec{0};
        static std::atomic<double> recv_per_sec{0};
        static std::atomic<double> send_per_sec{0};
        // start the sampling timer once, on the first /stats request
        static std::once_flag once;
        std::call_once(once, []() {
            struct Snapshot {
                uint64_t ms, connections, requests, recv_bytes, send_bytes;
            };
            auto last = std::make_shared<Snapshot>();
            const HttpServerStat& stat = g_server.getStat();
            last->ms = gettimeofday_ms();
            last->connections = stat.total_connections.load();
            last->requests    = stat.total_requests.load();
            last->recv_bytes  = stat.total_recv_bytes.load();
            last->send_bytes  = stat.total_send_bytes.load();
            hv::setInterval(60000, [last](hv::TimerID) {
                const HttpServerStat& stat = g_server.getStat();
                uint64_t now = gettimeofday_ms();
                double dt = (now - last->ms) / 1000.0;
                if (dt <= 0) return;
                uint64_t connections = stat.total_connections.load();
                uint64_t requests    = stat.total_requests.load();
                uint64_t recv_bytes  = stat.total_recv_bytes.load();
                uint64_t send_bytes  = stat.total_send_bytes.load();
                conn_per_sec = (connections - last->connections) / dt;
                req_per_sec  = (requests    - last->requests)    / dt;
                recv_per_sec = (recv_bytes  - last->recv_bytes)  / dt;
                send_per_sec = (send_bytes  - last->send_bytes)  / dt;
                last->ms = now;
                last->connections = connections;
                last->requests    = requests;
                last->recv_bytes  = recv_bytes;
                last->send_bytes  = send_bytes;
            });
        });

        const HttpServerStat& stat = g_server.getStat();
        hv::Json json;
        json["cur_connections"]   = stat.cur_connections.load();
        json["total_connections"] = stat.total_connections.load();
        json["total_requests"]    = stat.total_requests.load();
        json["total_recv_bytes"]  = stat.total_recv_bytes.load();
        json["total_send_bytes"]  = stat.total_send_bytes.load();
        // per-second rates over the last 60s window (0 until first window elapses)
        json["connections_per_sec"] = conn_per_sec.load();
        json["requests_per_sec"]    = req_per_sec.load();
        json["recv_bytes_per_sec"]  = recv_per_sec.load();
        json["send_bytes_per_sec"]  = send_per_sec.load();
        return resp->Json(json);
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

#ifdef WITH_LUA
    // curl -v "http://ip:port/lua/hello?id=42"
    router.GET("/lua/hello", HttpScriptHandler("examples/scripts/hello.lua"));
#endif
#ifdef WITH_JS
    // curl -v "http://ip:port/js/hello?id=42"
    router.GET("/js/hello", HttpScriptHandler("examples/scripts/hello.js"));
#endif
#if defined(WITH_LUA) || defined(WITH_JS)
    // curl -v "http://ip:port/script/hello?id=42"
    // curl -v "http://ip:port/script/async?host=example.com"  (sync-style async)
    router.Script("/script/", "examples/scripts");
#endif

    // curl -v http://ip:port/async
    router.GET("/async", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        writer->Begin();
        writer->WriteHeader("X-Response-tid", hv_gettid());
        writer->WriteHeader("Content-Type", "text/plain");
        writer->WriteBody("This is an async response.\n");
        writer->End();
    });

    // curl -v http://ip:port/close
    // Test HTTP_STATUS_CLOSE: closes connection without sending any response
    router.GET("/close", [](HttpRequest* req, HttpResponse* resp) {
        return HTTP_STATUS_CLOSE;
    });

    // middleware
    router.AllowCORS();
    router.Use([](HttpRequest* req, HttpResponse* resp) {
        resp->SetHeader("X-Request-tid", hv::to_string(hv_gettid()));
        return HTTP_STATUS_NEXT;
    });

    HttpServer& server = g_server;
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
