extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hvlua.h"

#ifdef HVLUA_WITH_REDIS

#include <cctype>
#include <string>
#include <vector>

#include "EventLoop.h"
#include "AsyncRedisClient.h"

using namespace hv;

// hv.redis — coroutine-synchronous Redis client. Single-loop model (mirrors
// hvlua_http.cpp): the AsyncRedisClient is bound to the CURRENT loop
// (currentThreadEventLoopPtr), so its command completion callback fires on this
// same loop thread — we resume the coroutine directly, no cross-thread hop.
//
// API (see docs/superpowers/specs/2026-07-28-lua-binding-design.md §4.6):
//   local r = hv.redis.new({ host=, port=, auth=, db=, timeout= })
//   local v, err = r:command("GET", "k")        -- variadic args
//   local v, err = r:command({"SET","k","v"})   -- or a single array table
//   r:get(k) / r:set(k,v) / r:del(k) ...        -- thin command() sugar
// Reply -> Lua value mapping (see reply_push): string/int/nil/array table;
// redis error reply -> (nil, "err message"). Transport failure -> (nil, err).

static const char* REDIS_CLIENT_MT = "hv.redis.client.mt";

struct LuaRedisClient {
    AsyncRedisClient* client;
};

static int redis_client_gc(lua_State* L) {
    LuaRedisClient* box = (LuaRedisClient*)luaL_checkudata(L, 1, REDIS_CLIENT_MT);
    if (box && box->client) {
        delete box->client;   // external loop (not owner): does NOT stop the shared loop
        box->client = NULL;
    }
    return 0;
}

// Push a RedisReply onto L as a native Lua value.
//   STRING -> string ; INTEGER -> integer ; NIL -> nil ;
//   ERROR  -> (handled by caller as nil,err) ; ARRAY -> table (1-based),
//   with nested nil elements represented as `false` (Lua arrays cannot hold nil).
static void reply_push(lua_State* L, const RedisReply& reply) {
    switch (reply.type) {
    case REDIS_REPLY_STRING:
        lua_pushlstring(L, reply.str.data(), reply.str.size());
        break;
    case REDIS_REPLY_INTEGER:
        lua_pushinteger(L, (lua_Integer)reply.integer);
        break;
    case REDIS_REPLY_ARRAY: {
        if (reply.null_array) {
            lua_pushnil(L);
            break;
        }
        lua_createtable(L, (int)reply.elements.size(), 0);
        for (size_t i = 0; i < reply.elements.size(); ++i) {
            const RedisReply& e = reply.elements[i];
            if (e.isNil()) {
                lua_pushboolean(L, 0);   // nil placeholder (keeps array contiguous)
            } else {
                reply_push(L, e);
            }
            lua_rawseti(L, -2, (int)(i + 1));
        }
        break;
    }
    case REDIS_REPLY_NIL:
    default:
        lua_pushnil(L);
        break;
    }
}

// hv.redis.new([cfg]) -> client userdata | nil, err
static int l_redis_new(lua_State* L) {
    EventLoopPtr loop = currentThreadEventLoopPtr;
    if (!loop) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.redis: no shared event loop on this thread");
        return 2;
    }

    std::string host = "127.0.0.1";
    int port = 6379;
    std::string auth;
    int db = 0;
    int timeout = 0;
    bool has_timeout = false;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "host");
        if (lua_isstring(L, -1)) host = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "port");
        if (lua_isinteger(L, -1)) port = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "auth");
        if (lua_isstring(L, -1)) auth = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "db");
        if (lua_isinteger(L, -1)) db = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "timeout");
        if (lua_isinteger(L, -1)) { timeout = (int)lua_tointeger(L, -1); has_timeout = true; }
        lua_pop(L, 1);
    }

    LuaRedisClient* box = (LuaRedisClient*)lua_newuserdata(L, sizeof(LuaRedisClient));
    box->client = new AsyncRedisClient(loop);   // bound to current loop, not owner
    box->client->setHost(host);
    box->client->setPort(port);
    if (!auth.empty()) box->client->setAuth(auth);
    if (db > 0) box->client->setDb(db);
    if (has_timeout) box->client->setTimeout(timeout);
    // The shared metatable (with __gc + methods) is created once in
    // hvlua_open_redis; just attach it here.
    luaL_setmetatable(L, REDIS_CLIENT_MT);
    // start the client now so the connection is established up front (the first
    // command would otherwise trigger startConnect lazily).
    box->client->start(false);
    return 1;
}

// Continuation for a command: (value) on success, or (nil, err) already on stack.
static int redis_cmd_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    return lua_gettop(L) >= 2 && lua_isnil(L, -2) ? 2 : 1;
}

// Build a RedisCommand from Lua args starting at `first`. Either a single array
// table {"GET","k"} or a variadic list "GET","k". Numbers are stringified.
static bool build_command(lua_State* L, int first, RedisCommand* cmd) {
    if (lua_istable(L, first) && lua_gettop(L) == first) {
        int n = (int)lua_rawlen(L, first);
        for (int i = 1; i <= n; ++i) {
            lua_rawgeti(L, first, i);
            size_t len = 0;
            const char* s = lua_tolstring(L, -1, &len);
            if (s == NULL) { lua_pop(L, 1); return false; }
            cmd->emplace_back(s, len);
            lua_pop(L, 1);
        }
    } else {
        int top = lua_gettop(L);
        for (int i = first; i <= top; ++i) {
            size_t len = 0;
            const char* s = lua_tolstring(L, i, &len);
            if (s == NULL) return false;
            cmd->emplace_back(s, len);
        }
    }
    return !cmd->empty();
}

// Shared implementation for r:command(...) and the sugar methods.
static int redis_do_command(lua_State* L, RedisCommand&& cmd) {
    LuaRedisClient* box = (LuaRedisClient*)luaL_checkudata(L, 1, REDIS_CLIENT_MT);
    if (box == NULL || box->client == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.redis: client closed");
        return 2;
    }

    HvLuaCoroutine* co = hvlua_suspend(L);
    box->client->command(cmd, [co](const RedisResult& result) {
        lua_State* cur = hvlua_coroutine_state(co);
        if (cur == NULL) { hvlua_cancel(co); return; }  // coroutine gone
        if (result.code != 0) {
            lua_pushnil(cur);
            lua_pushfstring(cur, "hv.redis: request failed (%d)", result.code);
            hvlua_resume(co, 2);
            return;
        }
        if (result.reply.isError()) {
            lua_pushnil(cur);
            lua_pushlstring(cur, result.reply.str.data(), result.reply.str.size());
            hvlua_resume(co, 2);
            return;
        }
        reply_push(cur, result.reply);
        hvlua_resume(co, 1);
    });
    return lua_yieldk(L, 0, (lua_KContext)0, redis_cmd_k);
}

// r:command("GET","k") | r:command({"GET","k"})
static int l_redis_command(lua_State* L) {
    RedisCommand cmd;
    if (!build_command(L, 2, &cmd)) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.redis: empty or invalid command");
        return 2;
    }
    return redis_do_command(L, std::move(cmd));
}

// Sugar: r:<verb>(args...) == r:command("<VERB>", args...). The verb string is
// carried as an upvalue set when the method is registered (see redis_methods).
static int l_redis_verb(lua_State* L) {
    const char* verb = lua_tostring(L, lua_upvalueindex(1));
    RedisCommand cmd;
    cmd.emplace_back(verb);
    int top = lua_gettop(L);
    for (int i = 2; i <= top; ++i) {
        size_t len = 0;
        const char* s = lua_tolstring(L, i, &len);
        if (s == NULL) {
            lua_pushnil(L);
            lua_pushstring(L, "hv.redis: invalid argument");
            return 2;
        }
        cmd.emplace_back(s, len);
    }
    return redis_do_command(L, std::move(cmd));
}

// verb sugar methods, registered as closures carrying the uppercase verb.
static const char* const redis_verbs[] = {
    "GET", "SET", "DEL", "INCR", "DECR", "EXPIRE", "EXISTS", NULL
};

static const luaL_Reg redis_methods[] = {
    { "command", l_redis_command },
    { NULL, NULL }
};

static const luaL_Reg redis_funcs[] = {
    { "new", l_redis_new },
    { NULL, NULL }
};

// Register redis methods (command + verb sugar) into the table on top of L.
static void register_redis_methods(lua_State* L) {
    luaL_setfuncs(L, redis_methods, 0);
    for (int i = 0; redis_verbs[i] != NULL; ++i) {
        // method name is the lowercase verb; closure upvalue is the UPPER verb.
        std::string name(redis_verbs[i]);
        for (char& c : name) c = (char)tolower((unsigned char)c);
        lua_pushstring(L, redis_verbs[i]);
        lua_pushcclosure(L, l_redis_verb, 1);
        lua_setfield(L, -2, name.c_str());
    }
}

extern "C" void hvlua_open_redis(lua_State* L) {
    // Create the shared client metatable once: __gc + __index=self + methods.
    if (luaL_newmetatable(L, REDIS_CLIENT_MT)) {
        lua_pushcfunction(L, redis_client_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");   // methods live on the metatable itself
        register_redis_methods(L);        // command + verb sugar into the mt
    }
    lua_pop(L, 1);

    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_newlib(L, redis_funcs);
    lua_setfield(L, -2, "redis");
    lua_setglobal(L, "hv");
}

#endif // HVLUA_WITH_REDIS
