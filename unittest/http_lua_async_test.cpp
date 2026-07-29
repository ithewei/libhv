/*
 * http_lua_async_test — Stage B integration test for coroutine-based async
 * Lua HTTP handlers.
 *
 * Starts a real HttpServer whose Lua route calls hv.sleep(ms) — a
 * synchronous-style API that yields the coroutine to the event loop. Fires
 * several concurrent requests and asserts:
 *   1. every response is correct (200 + expected body), proving the async
 *      writer path completes the deferred response;
 *   2. total wall time is far less than N * sleep, proving the requests are
 *      handled concurrently on one IO thread (coroutines interleave, the loop
 *      is never blocked).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <vector>

#include "hbase.h"
#include "hfile.h"
#include "hpath.h"
#include "htime.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "HttpScriptHandler.h"
#include "requests.h"

static std::string write_script(const char* name, const char* content) {
    hv_mkdir_p("tmp/http_lua_async_test");
    std::string path = HPath::join("tmp/http_lua_async_test", name);
    HFile file;
    int ret = file.open(path.c_str(), "wb");
    assert(ret == 0);
    file.write(content, strlen(content));
    file.close();
    return path;
}

int main() {
    // The handler sleeps 300ms inside the coroutine, then echoes the id.
    std::string script = write_script("sleep.lua",
        "function handle(ctx)\n"
        "  hv.sleep(300)\n"
        "  return ctx:json({ ok = true, id = ctx:query('id') })\n"
        "end\n");

    HttpService service;
    service.GET("/sleep", hv::HttpScriptHandler(script.c_str()));

    hv::HttpServer server(&service);
    server.setThreadNum(1);        // single IO thread: proves coroutine concurrency
    server.setPort(18080);
    server.start();
    hv_msleep(200);                // let the server come up

    const int N = 5;
    std::vector<std::thread> threads;
    std::atomic<int> ok_count{0};

    uint64_t start = gettimeofday_ms();
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([i, &ok_count]() {
            char url[128];
            snprintf(url, sizeof(url), "http://127.0.0.1:18080/sleep?id=%d", i);
            auto resp = requests::get(url);
            if (resp == NULL) return;
            if (resp->status_code != 200) return;
            char needle[32];
            snprintf(needle, sizeof(needle), "\"id\": \"%d\"", i);
            if (resp->body.find("\"ok\": true") != std::string::npos &&
                resp->body.find(needle) != std::string::npos) {
                ok_count++;
            }
        });
    }
    for (auto& t : threads) t.join();
    uint64_t elapsed = gettimeofday_ms() - start;

    server.stop();
    hv_msleep(100);

    printf("ok_count=%d/%d elapsed=%llums (each handler sleeps 300ms)\n",
           ok_count.load(), N, (unsigned long long)elapsed);
    assert(ok_count.load() == N);
    // Concurrent: total should be well under N*300ms. Allow generous slack for
    // CI, but it must be clearly less than serial execution (5*300=1500ms).
    assert(elapsed < 1200);
    printf("ALL http_lua_async_test PASSED\n");
    return 0;
}
