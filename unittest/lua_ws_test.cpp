/*
 * lua_ws_test — end-to-end test for the hv.ws Lua binding (hvlua_ws.cpp).
 *
 * Starts an in-process WebSocket echo server on its own thread, then runs a Lua
 * script on a shared-ptr EventLoop (single-loop model: WebSocketClient bound to
 * this loop; onopen/onmessage fire on the same thread and resume the coroutine).
 * Asserts hv.ws.connect + ws:send + ws:recv round-trips the echoed message.
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
#include "WebSocketServer.h"
#include "hvlua.h"

using namespace hv;

static std::string g_probe;
static int l_probe(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (!g_probe.empty()) g_probe += ",";
    g_probe.append(s, len);
    return 0;
}

int main() {
    // In-process WebSocket echo server on its own thread.
    WebSocketService ws;
    ws.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) {
        channel->send(msg);  // echo
    };
    hv::WebSocketServer server(&ws);
    server.setPort(20802);
    server.setThreadNum(1);
    server.start();
    hv_msleep(200);

    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();
    lua_State* L = hvlua_state(loop->loop());
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");

    int ret = hvlua_dostring(loop->loop(),
        "hv.setTimeout(1, function()\n"
        "  local ws, err = hv.ws.connect('ws://127.0.0.1:20802/')\n"
        "  if err then probe('err:'..err); hv.stop(); return end\n"
        "  probe('opened')\n"
        "  ws:send('hello')\n"
        "  local msg, rerr = ws:recv()\n"
        "  if rerr then probe('recverr:'..rerr) else probe(msg) end\n"
        "  ws:send('world')\n"
        "  local msg2 = ws:recv()\n"
        "  probe(msg2)\n"
        "  ws:close()\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(ret == 0);

    // hang guard.
    htimer_add(loop->loop(), [](htimer_t* t){ hloop_stop(hevent_loop(t)); }, 5000, 1);
    loop->run();
    loop.reset();
    server.stop();
    hv_msleep(100);

    printf("hv.ws result: %s\n", g_probe.c_str());
    assert(g_probe == "opened,hello,world");
    printf("ALL lua_ws_test PASSED\n");
    return 0;
}
