/*
 * lua_http_test — unit test for the hv.http Lua binding (hvlua_http.cpp).
 *
 * Starts an in-process HttpServer, then runs a Lua script on a shared-ptr
 * EventLoop (the single-loop model: AsyncHttpClient is bound to this loop, so
 * its completion callback fires on the same thread and resumes the coroutine
 * directly). Asserts hv.http.get returns the expected status/body.
 */

#include <assert.h>
#include <stdio.h>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hloop.h"
#include "EventLoop.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "hvlua.h"

static std::string g_probe;
static int l_probe(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (!g_probe.empty()) g_probe += ",";
    g_probe.append(s, len);
    return 0;
}

int main() {
    // In-process HTTP server on its own thread.
    HttpService service;
    service.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        resp->body = "pong";
        return 200;
    });
    hv::HttpServer server(&service);
    server.setPort(20801);
    server.setThreadNum(1);
    server.start();
    hv_msleep(200);

    // hvlua-style runtime loop: a default-constructed EventLoop owns its own
    // hloop (auto-freed) and, via shared_ptr, lets currentThreadEventLoopPtr /
    // shared_from_this() hand this loop to the per-state AsyncHttpClient.
    // EventLoop::run() publishes it as this thread's loop (TLS).
    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();
    lua_State* L = hvlua_state(loop->loop());
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");

    int ret = hvlua_dostring(loop->loop(),
        "hv.setTimeout(1, function()\n"
        "  local resp, err = hv.http.get('http://127.0.0.1:20801/ping')\n"
        "  if err then probe('err:'..err)\n"
        "  else probe(tostring(resp.status)); probe(resp.body) end\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(ret == 0);
    loop->run();

    loop.reset();
    server.stop();
    hv_msleep(100);

    printf("hv.http result: %s\n", g_probe.c_str());
    assert(g_probe == "200,pong");
    printf("ALL lua_http_test PASSED\n");
    return 0;
}
