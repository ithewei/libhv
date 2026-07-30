/*
 * lua_redis_test — unit test for the hv.redis Lua binding (hvlua_redis.cpp).
 *
 * Starts an in-process FakeRedisServer, then runs a Lua script on a shared-ptr
 * EventLoop (single-loop model: AsyncRedisClient is bound to this loop, so its
 * command completion callback fires on the same thread and resumes the coroutine
 * directly). Asserts hv.redis command + verb sugar map replies to Lua values,
 * and that an error reply comes back as (nil, err).
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
#include "hvlua.h"
#include "redis_test_server.h"

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
    // In-process fake redis server: PING->PONG, SET->OK, GET k -> "v",
    // INCR -> 1, anything else -> error reply.
    FakeRedisServer server;
    server.setCommandHandler([](const RedisCommand& cmd) {
        RedisReply reply;
        if (cmd[0] == "PING") {
            reply.type = REDIS_REPLY_STRING; reply.str = "PONG";
        } else if (cmd[0] == "SET") {
            reply.type = REDIS_REPLY_STRING; reply.str = "OK";
        } else if (cmd[0] == "GET") {
            reply.type = REDIS_REPLY_STRING; reply.str = "v"; reply.bulk = true;
        } else if (cmd[0] == "INCR") {
            reply.type = REDIS_REPLY_INTEGER; reply.integer = 1;
        } else {
            reply.type = REDIS_REPLY_ERROR; reply.str = "ERR unsupported";
        }
        return reply;
    });
    server.start();

    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();
    lua_State* L = hvlua_state(loop->loop());
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");
    lua_pushinteger(L, server.port());
    lua_setglobal(L, "REDIS_PORT");

    int ret = hvlua_dostring(loop->loop(),
        "hv.setTimeout(1, function()\n"
        "  local r = hv.redis.new({ host='127.0.0.1', port=REDIS_PORT })\n"
        "  local ok = r:set('k', 'v'); probe(ok)\n"           // OK
        "  local v = r:get('k'); probe(v)\n"                   // v
        "  local n = r:incr('c'); probe(tostring(n))\n"        // 1
        "  local cmd = r:command({'PING'}); probe(cmd)\n"      // PONG
        "  local bad, err = r:command('BADCMD')\n"             // nil, "ERR unsupported"
        "  if bad == nil then probe('err:'..err) end\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(ret == 0);
    loop->run();

    loop.reset();
    server.stop();

    printf("hv.redis result: %s\n", g_probe.c_str());
    assert(g_probe == "OK,v,1,PONG,err:ERR unsupported");
    printf("ALL lua_redis_test PASSED\n");
    return 0;
}
