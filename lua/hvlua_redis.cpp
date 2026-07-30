extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hvlua.h"

#ifdef HVLUA_WITH_REDIS

#include <cctype>
#include <memory>
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
    // Set true by __gc before deleting the client. An in-flight command's
    // completion callback checks this: destroying the client runs
    // ~AsyncRedisClient -> stop(true) -> failPending on THIS (loop) thread, which
    // fires the pending callbacks synchronously from inside __gc. Resuming a
    // coroutine (lua_resume) from within a __gc metamethod is illegal, so when
    // destroyed we only release the suspend token (hvlua_cancel is __gc-safe: it
    // does luaL_unref + free, no lua_resume) and skip the resume.
    bool destroyed;
};

static int redis_client_gc(lua_State* L) {
    LuaRedisClient* box = (LuaRedisClient*)luaL_checkudata(L, 1, REDIS_CLIENT_MT);
    if (box && box->client) {
        box->destroyed = true;   // neutralize in-flight callbacks before teardown
        delete box->client;      // external loop (not owner): does NOT stop the shared loop
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
    box->destroyed = false;
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

// Tracks whether a command's completion callback fired synchronously (inline,
// before the coroutine yields) vs asynchronously (later, on the loop).
struct RedisCmdState {
    HvLuaCoroutine* co;       // suspend token (async path)
    lua_State*      L;        // the running coroutine (sync path pushes here)
    bool            yielded;  // set true once we know the coroutine yielded
    bool            done;     // callback fired
    int             nresults; // results pushed (sync path)
};

// Push the (value) / (nil,err) results for a reply onto `st`'s coroutine stack.
// Returns the number of values pushed.
static int redis_push_result(lua_State* co, const RedisResult& result) {
    if (result.code != 0) {
        lua_pushnil(co);
        lua_pushfstring(co, "hv.redis: request failed (%d)", result.code);
        return 2;
    }
    if (result.reply.isError()) {
        lua_pushnil(co);
        lua_pushlstring(co, result.reply.str.data(), result.reply.str.size());
        return 2;
    }
    reply_push(co, result.reply);
    return 1;
}

// Shared implementation for r:command(...) and the sugar methods.
static int redis_do_command(lua_State* L, RedisCommand&& cmd) {
    LuaRedisClient* box = (LuaRedisClient*)luaL_checkudata(L, 1, REDIS_CLIENT_MT);
    if (box == NULL || box->client == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.redis: client closed");
        return 2;
    }

    // AsyncRedisClient::command() may invoke the callback SYNCHRONOUSLY (e.g.
    // enqueue rejected because the loop is not running). If that happens after
    // hvlua_suspend() but before lua_yieldk(), resuming the still-running
    // coroutine is illegal. So track sync vs async completion and, on sync
    // completion, return the results directly instead of yielding.
    auto st = std::make_shared<RedisCmdState>();
    st->co = hvlua_suspend(L);
    st->L = L;
    st->yielded = false;
    st->done = false;
    st->nresults = 0;

    // NOTE: lua_yieldk never returns to this C++ frame (longjmp in a C-built
    // Lua), so destructors of non-trivial locals in this frame are SKIPPED and
    // would leak. Confine `cmd` (the RedisCommand this frame owns) and our local
    // `st` ref to a scope that ends BEFORE the yield: the command callback holds
    // its own `st` ref, and command() copies what it needs from cmd.
    bool done;
    int nresults;
    {
        RedisCommand local_cmd(std::move(cmd));  // owned here, destroyed at block end
        box->client->command(local_cmd, [st, box](const RedisResult& result) {
            // Client being destroyed (callback fires from ~AsyncRedisClient ->
            // stop -> failPending inside the Lua __gc metamethod): never resume.
            if (box->destroyed) { hvlua_cancel(st->co); st->co = NULL; return; }
            if (!st->yielded) {
                // Synchronous completion: coroutine hasn't yielded yet. Push
                // results onto its stack; redis_do_command returns them. Do NOT
                // resume (still running). Release the unused suspend token.
                st->done = true;
                st->nresults = redis_push_result(st->L, result);
                hvlua_cancel(st->co);
                st->co = NULL;
                return;
            }
            // Async completion on the loop: resume the suspended coroutine.
            lua_State* cur = hvlua_coroutine_state(st->co);
            if (cur == NULL) { hvlua_cancel(st->co); st->co = NULL; return; }
            int n = redis_push_result(cur, result);
            HvLuaCoroutine* tok = st->co;
            st->co = NULL;
            hvlua_resume(tok, n);
        });
        // Snapshot sync-completion state into POD locals, then mark yielded and
        // drop this frame's `st` ref (and `local_cmd`) before the yield.
        done = st->done;
        nresults = st->nresults;
        if (!done) st->yielded = true;
        st.reset();
    }

    if (done) {
        // Completed synchronously: results are already on L. Return them now.
        return nresults;
    }
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
