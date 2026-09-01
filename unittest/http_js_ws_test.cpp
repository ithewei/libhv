/*
 * http_js_ws_test - HttpJsHandler + hv/ws Promise binding.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hbase.h"
#include "hfile.h"
#include "hpath.h"
#include "htime.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "HttpJsHandler.h"
#include "HttpScriptHandler.h"
#include "WebSocketServer.h"
#include "requests.h"

#define CHECK(expr)                                                                    \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            fprintf(stderr, "CHECK failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static std::string write_script(const char* name, const char* content) {
    hv_mkdir_p("tmp/http_js_ws_test");
    std::string path = HPath::join("tmp/http_js_ws_test", name);
    HFile file;
    int ret = file.open(path.c_str(), "wb");
    CHECK(ret == 0);
    file.write(content, strlen(content));
    file.close();
    return path;
}

int main() {
    WebSocketService ws_service;
    ws_service.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) { channel->send(msg); };
    WebSocketService idle_ws_service;
    hv::WebSocketServer ws_server(&ws_service);
    ws_server.setPort(0);
    ws_server.setThreadNum(1);
    CHECK(ws_server.start() == 0);
    CHECK(ws_server.port > 0);
    hv::WebSocketServer idle_ws_server(&idle_ws_service);
    idle_ws_server.setPort(0);
    idle_ws_server.setThreadNum(1);
    CHECK(idle_ws_server.start() == 0);
    CHECK(idle_ws_server.port > 0);

    char script_buf[1024];
    snprintf(script_buf, sizeof(script_buf),
             "const wsmod = require('hv/ws');\n"
             "async function get(ctx) {\n"
             "  const ws = await wsmod.connect('ws://127.0.0.1:%d/', { timeout: 500, ping_interval: 100 });\n"
             "  ws.send('hello-js');\n"
             "  const msg = await ws.recv();\n"
             "  ws.close();\n"
             "  return { ok: true, msg };\n"
             "}\n",
             ws_server.port);
    std::string script = write_script("ws.js", script_buf);
    snprintf(script_buf, sizeof(script_buf),
             "const wsmod = require('hv/ws');\n"
             "async function get(ctx) {\n"
             "  const ws = await wsmod.connect('ws://127.0.0.1:%d/', { timeout: 500, ping_interval: 100 });\n"
             "  const msg = await ws.recv();\n"
             "  ws.close();\n"
             "  return { ok: true, msg };\n"
             "}\n",
             idle_ws_server.port);
    std::string idle_script = write_script("ws_idle.js", script_buf);
    snprintf(script_buf, sizeof(script_buf),
             "const wsmod = require('hv/ws');\n"
             "async function get(ctx) {\n"
             "  const ws = await wsmod.connect('ws://127.0.0.1:%d/', { timeout: 500, ping_interval: 100 });\n"
             "  ws.recv();\n"
             "  return { ok: true };\n"
             "}\n",
             idle_ws_server.port);
    std::string fireforget_script = write_script("ws_fireforget.js", script_buf);

    HttpService service;
    service.GET("/ws", hv::HttpScriptHandler(script.c_str()));
    hv::HttpJsHandlerOptions timeout_options;
    timeout_options.timeout_ms = 100;
    service.GET("/ws_idle", hv::HttpJsHandler(idle_script.c_str(), timeout_options));
    service.GET("/ws_fireforget", hv::HttpJsHandler(fireforget_script.c_str(), timeout_options));

    hv::HttpServer server(&service);
    server.setThreadNum(1);
    server.setPort(0);
    CHECK(server.start() == 0);
    CHECK(server.port > 0);
    hv_msleep(200);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ws", server.port);
    auto resp = requests::get(url);
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ws_idle", server.port);
    uint64_t idle_start = gettimeofday_ms();
    auto idle_resp = requests::get(url);
    uint64_t idle_elapsed = gettimeofday_ms() - idle_start;
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ws_fireforget", server.port);
    auto fireforget_resp = requests::get(url);
    server.stop();
    ws_server.stop();
    idle_ws_server.stop();
    hv_msleep(100);

    CHECK(resp != NULL);
    CHECK(resp->status_code == 200);
    CHECK(resp->body.find("\"ok\":true") != std::string::npos);
    CHECK(resp->body.find("\"msg\":\"hello-js\"") != std::string::npos);
    CHECK(idle_resp != NULL);
    CHECK(idle_resp->status_code == 500);
    CHECK(idle_resp->body == "javascript handler error");
    CHECK(idle_elapsed < 1000);
    CHECK(fireforget_resp != NULL);
    CHECK(fireforget_resp->status_code == 200);
    CHECK(fireforget_resp->body.find("\"ok\":true") != std::string::npos);
    printf("ALL http_js_ws_test PASSED\n");
    return 0;
}
