extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hvlua.h"

#ifdef HVLUA_WITH_HTTP

#include <deque>
#include <memory>
#include <string>

#include "EventLoop.h"
#include "WebSocketClient.h"

using namespace hv;

// hv.ws — coroutine-synchronous WebSocket client. Single-loop model (mirrors
// hvlua_http.cpp / hvlua_redis.cpp): the WebSocketClient is bound to the CURRENT
// loop, so onopen/onmessage/onclose fire on this same loop thread and resume the
// coroutine directly, no cross-thread hop.
//
// WebSocket is message-DRIVEN (the peer may push at any time), unlike the
// request/response clients. So instead of a per-call callback we buffer inbound
// messages in a queue and expose a coroutine-synchronous ws:recv():
//   local ws, err = hv.ws.connect("ws://127.0.0.1:8888/path")
//   ws:send("hello")
//   local msg, err = ws:recv()   -- suspends until a message arrives / closed
//   ws:close()
//
// recv() returns a buffered message immediately if one is queued; otherwise it
// suspends the coroutine until onmessage (resume with msg) or onclose (resume
// with nil,"closed"). Only one recv() may be pending at a time.

static const char* WS_CLIENT_MT = "hv.ws.client.mt";

typedef std::deque<std::string> WsInbox;

struct LuaWsClient {
    WebSocketClient*        client;
    WsInbox                 inbox;    // buffered inbound messages
    HvLuaCoroutine*         recv_co;   // coroutine waiting in recv(), or NULL
    bool                    connected; // handshake completed (reset on close)
    bool                    reconnect; // auto-reconnect enabled
    bool                    opened_once; // onopen has fired at least once
};

// Resume a pending recv() coroutine (if any) with the front queued message, or
// with an error when the socket is gone: (nil,"reconnecting") if auto-reconnect
// is enabled (transient), else (nil,"closed") (terminal).
static void ws_try_deliver(LuaWsClient* box) {
    if (box->recv_co == NULL) return;
    lua_State* co = hvlua_coroutine_state(box->recv_co);
    if (co == NULL) {                 // coroutine was GC'd / stale
        hvlua_cancel(box->recv_co);
        box->recv_co = NULL;
        return;
    }
    if (!box->inbox.empty()) {
        std::string msg = std::move(box->inbox.front());
        box->inbox.pop_front();
        HvLuaCoroutine* co_tok = box->recv_co;
        box->recv_co = NULL;
        lua_pushlstring(co, msg.data(), msg.size());
        hvlua_resume(co_tok, 1);
    } else if (!box->connected) {
        HvLuaCoroutine* co_tok = box->recv_co;
        box->recv_co = NULL;
        lua_pushnil(co);
        // Distinguish a transient disconnect (reconnecting) from a terminal
        // close so the script can choose to keep waiting or stop.
        lua_pushstring(co, box->reconnect ? "reconnecting" : "closed");
        hvlua_resume(co_tok, 2);
    }
}

static int ws_client_gc(lua_State* L) {
    LuaWsClient* box = (LuaWsClient*)luaL_checkudata(L, 1, WS_CLIENT_MT);
    if (box) {
        if (box->recv_co) {           // release a still-suspended recv token
            hvlua_cancel(box->recv_co);
            box->recv_co = NULL;
        }
        if (box->client) {
            delete box->client;       // external loop (not owner): does NOT stop it
            box->client = NULL;
        }
        box->inbox.~WsInbox();        // placement-constructed; destroy explicitly
    }
    return 0;
}

// Continuation for connect: (ws) on success, or (nil,err) already on stack.
static int ws_connect_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
        lua_pop(L, 1);              // drop the `true` success marker
        lua_pushvalue(L, 1);        // return the ws userdata (self, stack slot 1)
        return 1;
    }
    return 2;                       // (nil, err)
}

// Parse an optional { reconnect = { min_delay, max_delay, delay_policy,
// max_retry } } sub-table at opts_index into *out. Returns true if reconnect
// was requested.
static bool ws_parse_reconnect(lua_State* L, int opts_index, reconn_setting_t* out) {
    if (!lua_istable(L, opts_index)) return false;
    lua_getfield(L, opts_index, "reconnect");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    reconn_setting_init(out);
    lua_getfield(L, -1, "min_delay");
    if (lua_isinteger(L, -1)) out->min_delay = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "max_delay");
    if (lua_isinteger(L, -1)) out->max_delay = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "delay_policy");
    if (lua_isinteger(L, -1)) out->delay_policy = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "max_retry");
    if (lua_isinteger(L, -1)) out->max_retry_cnt = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pop(L, 1);   // pop the reconnect table
    return true;
}

// hv.ws.connect(url [, opts]) -> ws | nil, err
// opts = { headers = {..}, ping_interval = ms, reconnect = {min_delay, max_delay,
//          delay_policy, max_retry} }
static int l_ws_connect(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    EventLoopPtr loop = currentThreadEventLoopPtr;
    if (!loop) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.ws: no shared event loop on this thread");
        return 2;
    }

    // userdata carries the client + inbox; placement-new the non-POD members.
    LuaWsClient* box = (LuaWsClient*)lua_newuserdata(L, sizeof(LuaWsClient));
    new (&box->inbox) WsInbox();
    box->recv_co = NULL;
    box->connected = false;
    box->reconnect = false;
    box->opened_once = false;
    box->client = new WebSocketClient(loop);   // bound to current loop, not owner
    luaL_setmetatable(L, WS_CLIENT_MT);
    lua_replace(L, 1);              // move ws userdata to slot 1 for ws_connect_k

    // opts (arg 2): headers sub-table, ping_interval, reconnect sub-table.
    http_headers headers = DefaultHeaders;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) {
                    headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);   // pop headers (or nil)
        lua_getfield(L, 2, "ping_interval");
        if (lua_isinteger(L, -1)) box->client->setPingInterval((int)lua_tointeger(L, -1));
        lua_pop(L, 1);
        reconn_setting_t reconn;
        if (ws_parse_reconnect(L, 2, &reconn)) {
            box->reconnect = true;
            box->client->setReconnect(&reconn);
        }
    }

    box->client->onopen = [box]() {
        // Connection established (initial or after a reconnect): mark connected
        // and reset for the (possibly reused) session.
        box->connected = true;
        if (!box->opened_once) {
            // Initial connect: resume the connect() coroutine held in recv_co.
            box->opened_once = true;
            lua_State* co = hvlua_coroutine_state(box->recv_co);
            if (co == NULL) { return; }
            HvLuaCoroutine* tok = box->recv_co;
            box->recv_co = NULL;
            lua_pushboolean(co, 1);     // success marker for ws_connect_k
            hvlua_resume(tok, 1);
        }
        // A reconnect's onopen does not resume anything: a recv() waiter (if any)
        // stays parked and is woken by the next onmessage.
    };
    box->client->onmessage = [box](const std::string& msg) {
        box->inbox.push_back(msg);
        ws_try_deliver(box);
    };
    box->client->onclose = [box]() {
        box->connected = false;
        // Wake whoever is waiting. On the initial connect this is the connect()
        // coroutine (handshake failed -> never opened_once); ws_try_deliver
        // resumes it with (nil, "closed"/"reconnecting"). After a successful
        // open it's a pending recv(). With auto-reconnect on, the socket will
        // retry underneath and a future onopen/onmessage resumes fresh recvs.
        ws_try_deliver(box);
    };

    box->recv_co = hvlua_suspend(L);   // reuse recv_co to hold the connect wait
    // NOTE: lua_yieldk below never returns to this C++ frame (longjmp in a
    // C-built Lua), so destructors of locals here are SKIPPED. Scope the
    // non-trivial `headers` (std::map) so it is destroyed BEFORE the yield;
    // open() copies what it needs. Keep only the trivial `ret` for the check.
    int ret;
    {
        http_headers headers = DefaultHeaders;
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "headers");
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) {
                        headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);   // pop headers (or nil)
        }
        ret = box->client->open(url, headers);
    }   // ~headers runs here, before the yield
    if (ret != 0) {
        hvlua_cancel(box->recv_co);
        box->recv_co = NULL;
        lua_pushnil(L);
        lua_pushfstring(L, "hv.ws: open failed (%d)", ret);
        return 2;
    }
    return lua_yieldk(L, 0, (lua_KContext)0, ws_connect_k);
}

// Continuation for recv: (msg) or (nil,err) already on stack.
static int ws_recv_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    return lua_gettop(L) >= 2 && lua_isnil(L, -2) ? 2 : 1;
}

// ws:recv() -> msg | nil, err  (coroutine-synchronous)
static int l_ws_recv(lua_State* L) {
    LuaWsClient* box = (LuaWsClient*)luaL_checkudata(L, 1, WS_CLIENT_MT);
    if (box == NULL || box->client == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    if (box->recv_co != NULL) {
        lua_pushnil(L); lua_pushstring(L, "hv.ws: recv already pending"); return 2;
    }
    // fast path: a message is already queued -> return it without suspending.
    if (!box->inbox.empty()) {
        std::string msg = std::move(box->inbox.front());
        box->inbox.pop_front();
        lua_pushlstring(L, msg.data(), msg.size());
        return 1;
    }
    if (!box->connected) {
        lua_pushnil(L);
        lua_pushstring(L, box->reconnect ? "reconnecting" : "closed");
        return 2;
    }
    box->recv_co = hvlua_suspend(L);
    return lua_yieldk(L, 0, (lua_KContext)0, ws_recv_k);
}

// ws:send(msg [, "binary"]) -> nbytes | nil, err  (non-blocking, no suspend)
static int l_ws_send(lua_State* L) {
    LuaWsClient* box = (LuaWsClient*)luaL_checkudata(L, 1, WS_CLIENT_MT);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    if (box == NULL || box->client == NULL || !box->connected) {
        // Not connected (never opened, or between reconnect attempts): sending
        // now would silently drop, so report it instead.
        lua_pushnil(L);
        lua_pushstring(L, (box && box->reconnect) ? "reconnecting" : "closed");
        return 2;
    }
    enum ws_opcode opcode = WS_OPCODE_TEXT;
    if (lua_isstring(L, 3) && std::string(lua_tostring(L, 3)) == "binary") {
        opcode = WS_OPCODE_BINARY;
    }
    int n = box->client->send(data, (int)len, opcode);
    if (n < 0) {
        lua_pushnil(L); lua_pushstring(L, "hv.ws: send failed"); return 2;
    }
    lua_pushinteger(L, n);
    return 1;
}

// ws:close()
static int l_ws_close(lua_State* L) {
    LuaWsClient* box = (LuaWsClient*)luaL_checkudata(L, 1, WS_CLIENT_MT);
    if (box && box->client) {
        // Explicit close is a terminal user action: disable auto-reconnect so
        // recv() reports "closed" (not "reconnecting") and the socket stays down.
        box->reconnect = false;
        box->connected = false;
        box->client->close();
    }
    return 0;
}

static const luaL_Reg ws_methods[] = {
    { "recv",  l_ws_recv  },
    { "send",  l_ws_send  },
    { "close", l_ws_close },
    { NULL, NULL }
};

static const luaL_Reg ws_funcs[] = {
    { "connect", l_ws_connect },
    { NULL, NULL }
};

extern "C" void hvlua_open_ws(lua_State* L) {
    if (luaL_newmetatable(L, WS_CLIENT_MT)) {
        lua_pushcfunction(L, ws_client_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, ws_methods, 0);
    }
    lua_pop(L, 1);

    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_newlib(L, ws_funcs);
    lua_setfield(L, -2, "ws");
    lua_setglobal(L, "hv");
}

#endif // HVLUA_WITH_HTTP
