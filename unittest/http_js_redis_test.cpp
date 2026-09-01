/*
 * http_js_redis_test - HttpJsHandler + hv/redis Promise binding.
 *
 * Uses the in-process FakeRedisServer so the test does not depend on a local
 * Redis daemon. The JS handler awaits Redis command promises and returns JSON.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hbase.h"
#include "hfile.h"
#include "hpath.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "HttpScriptHandler.h"
#include "requests.h"
#include "redis_test_server.h"

#define CHECK(expr)                                                                    \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            fprintf(stderr, "CHECK failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static std::string write_script(const char* name, const char* content) {
    hv_mkdir_p("tmp/http_js_redis_test");
    std::string path = HPath::join("tmp/http_js_redis_test", name);
    HFile file;
    int ret = file.open(path.c_str(), "wb");
    CHECK(ret == 0);
    file.write(content, strlen(content));
    file.close();
    return path;
}

int main() {
    FakeRedisServer redis_server;
    redis_server.setCommandHandler([](const hv::RedisCommand& cmd) {
        hv::RedisReply reply;
        if (cmd[0] == "PING") {
            reply.type = hv::REDIS_REPLY_STRING;
            reply.str = "PONG";
        }
        else if (cmd[0] == "SET") {
            reply.type = hv::REDIS_REPLY_STRING;
            reply.str = "OK";
        }
        else if (cmd[0] == "GET") {
            reply.type = hv::REDIS_REPLY_STRING;
            reply.str = "v";
            reply.bulk = true;
        }
        else if (cmd[0] == "INCR") {
            reply.type = hv::REDIS_REPLY_INTEGER;
            reply.integer = 1;
        }
        else {
            reply.type = hv::REDIS_REPLY_ERROR;
            reply.str = "ERR unsupported";
        }
        return reply;
    });
    redis_server.start();
    CHECK(redis_server.port() > 0);

    char script_buf[2048];
    snprintf(script_buf, sizeof(script_buf),
             "const redis = require('hv/redis');\n"
             "async function get(ctx) {\n"
             "  const r = redis.new({ host: '127.0.0.1', port: %d, timeout: 3000 });\n"
             "  const ok = await r.set('k', 'v');\n"
             "  const v = await r.get('k');\n"
             "  const n = await r.incr('c');\n"
             "  const pong = await r.command(['PING']);\n"
             "  let err = '';\n"
             "  try { await r.command('BADCMD'); } catch (e) { err = String(e); }\n"
             "  return { ok, v, n, pong, err };\n"
             "}\n",
             redis_server.port());
    std::string script = write_script("redis.js", script_buf);

    HttpService service;
    service.GET("/redis", hv::HttpScriptHandler(script.c_str()));

    hv::HttpServer server(&service);
    server.setThreadNum(1);
    server.setPort(0);
    CHECK(server.start() == 0);
    CHECK(server.port > 0);
    hv_msleep(200);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/redis", server.port);
    auto resp = requests::get(url);
    server.stop();
    redis_server.stop();
    hv_msleep(100);

    CHECK(resp != NULL);
    CHECK(resp->status_code == 200);
    CHECK(resp->body.find("\"ok\":\"OK\"") != std::string::npos);
    CHECK(resp->body.find("\"v\":\"v\"") != std::string::npos);
    CHECK(resp->body.find("\"n\":1") != std::string::npos);
    CHECK(resp->body.find("\"pong\":\"PONG\"") != std::string::npos);
    CHECK(resp->body.find("\"err\":\"ERR unsupported\"") != std::string::npos);
    printf("ALL http_js_redis_test PASSED\n");
    return 0;
}
