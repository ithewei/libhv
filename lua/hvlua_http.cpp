extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hvlua.h"

#ifdef HVLUA_WITH_HTTP

#include <string>

#include "hlog.h"
#include "EventLoop.h"
#include "AsyncHttpClient.h"

using namespace hv;

// hv.http.get/post/request(...) -> { status=, body=, headers={} } | nil, err
//
// Coroutine-synchronous HTTP client. Single-loop model: the AsyncHttpClient is
// bound to the CURRENT loop (currentThreadEventLoopPtr), so its completion
// callback fires on this same loop thread — we resume the coroutine directly,
// no cross-thread hop, no data copy. One client per lua_State (lazily created,
// owned by a registry userdata with __gc).

static const char* HTTP_CLIENT_REG = "hv.http.client";

struct LuaHttpClientBox {
    AsyncHttpClient* client;
};

static int http_client_gc(lua_State* L) {
    LuaHttpClientBox* box = (LuaHttpClientBox*)lua_touserdata(L, 1);
    if (box && box->client) {
        delete box->client;   // is_loop_owner=false: does NOT stop the shared loop
        box->client = NULL;
    }
    return 0;
}

// Get (lazily create) the per-lua_State AsyncHttpClient bound to the current loop.
// Returns NULL if there is no shared current loop.
static AsyncHttpClient* get_http_client(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, HTTP_CLIENT_REG);
    if (lua_isuserdata(L, -1)) {
        LuaHttpClientBox* box = (LuaHttpClientBox*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        return box->client;
    }
    lua_pop(L, 1);

    EventLoopPtr loop = currentThreadEventLoopPtr;
    if (!loop) return NULL;

    LuaHttpClientBox* box = (LuaHttpClientBox*)lua_newuserdata(L, sizeof(LuaHttpClientBox));
    box->client = new AsyncHttpClient(loop);   // bound to current loop, not owner
    if (luaL_newmetatable(L, "hv.http.client.mt")) {
        lua_pushcfunction(L, http_client_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, HTTP_CLIENT_REG);
    return box->client;
}

// Push { status, body, headers } for a response onto L.
static void push_response(lua_State* L, const HttpResponsePtr& resp) {
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, resp->status_code);
    lua_setfield(L, -2, "status");
    lua_pushlstring(L, resp->body.data(), resp->body.size());
    lua_setfield(L, -2, "body");
    lua_createtable(L, 0, (int)resp->headers.size());
    for (auto& kv : resp->headers) {
        lua_pushlstring(L, kv.second.data(), kv.second.size());
        lua_setfield(L, -2, kv.first.c_str());
    }
    lua_setfield(L, -2, "headers");
}

static int http_result_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    return lua_gettop(L) >= 2 && lua_isnil(L, -2) ? 2 : 1;
}

static int do_request(lua_State* L, http_method method, int url_index) {
    const char* url = luaL_checkstring(L, url_index);
    AsyncHttpClient* client = get_http_client(L);
    if (client == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.http: no shared event loop on this thread");
        return 2;
    }

    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    // optional body
    if (!lua_isnoneornil(L, url_index + 1)) {
        size_t len = 0;
        const char* body = lua_tolstring(L, url_index + 1, &len);
        if (body) req->body.assign(body, len);
    }
    // optional headers table
    if (lua_istable(L, url_index + 2)) {
        lua_pushnil(L);
        while (lua_next(L, url_index + 2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) {
                req->headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
    }

    HvLuaCoroutine* co = hvlua_suspend(L);
    client->send(req, [co](const HttpResponsePtr& resp) {
        // Same loop thread (client bound to current loop): resume directly.
        lua_State* cur = hvlua_coroutine_state(co);
        if (cur == NULL) { hvlua_cancel(co); return; }  // coroutine gone
        if (resp) {
            push_response(cur, resp);
            hvlua_resume(co, 1);
        } else {
            lua_pushnil(cur);
            lua_pushstring(cur, "hv.http: request failed");
            hvlua_resume(co, 2);
        }
    });
    return lua_yieldk(L, 0, (lua_KContext)0, http_result_k);
}

static int l_http_get(lua_State* L)    { return do_request(L, HTTP_GET, 1); }
static int l_http_post(lua_State* L)   { return do_request(L, HTTP_POST, 1); }
static int l_http_put(lua_State* L)    { return do_request(L, HTTP_PUT, 1); }
static int l_http_delete(lua_State* L) { return do_request(L, HTTP_DELETE, 1); }

// hv.http.request("GET", url, [body], [headers])
static int l_http_request(lua_State* L) {
    const char* m = luaL_checkstring(L, 1);
    return do_request(L, http_method_enum(m), 2);
}

static const luaL_Reg http_funcs[] = {
    { "request", l_http_request },
    { "get",     l_http_get     },
    { "post",    l_http_post    },
    { "put",     l_http_put     },
    { "delete",  l_http_delete  },
    { NULL, NULL }
};

extern "C" void hvlua_open_http(lua_State* L) {
    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_newlib(L, http_funcs);
    lua_setfield(L, -2, "http");
    lua_setglobal(L, "hv");
}

#endif // HVLUA_WITH_HTTP
