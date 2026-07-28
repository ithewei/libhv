#include "hv_lua.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hdns.h"
#include "hsocket.h"

namespace hv {

// hv.dns.resolve(host [, opt]) -> { ip, ip, ... } | nil, err
//
// Coroutine-synchronous: suspends the running coroutine, issues an async hdns
// query on the loop, and resumes with the resolved addresses (or nil,err) when
// the callback fires on the same loop thread.

struct DnsCtx {
    HvLuaCoroutine* co;
};

static void on_dns_resolved(hdns_t* query, const hdns_result_t* result, void* userdata) {
    (void)query;
    DnsCtx* d = (DnsCtx*)userdata;
    HvLuaCoroutine* co = d->co;
    delete d;

    lua_State* L = hvlua_coroutine_state(co);
    if (L == NULL) {   // coroutine gone (loop teardown); nothing to resume
        return;
    }

    if (result->status == HDNS_STATUS_OK && result->naddrs > 0) {
        lua_createtable(L, result->naddrs, 0);
        for (int i = 0; i < result->naddrs; ++i) {
            char ip[64] = {0};
            sockaddr_ip((sockaddr_u*)&result->addrs[i], ip, sizeof(ip));
            lua_pushstring(L, ip);
            lua_seti(L, -2, i + 1);
        }
        hvlua_resume(co, 1);        // one result: the address table
    } else {
        lua_pushnil(L);
        lua_pushfstring(L, "dns resolve failed: status=%d", result->status);
        hvlua_resume(co, 2);        // nil, err
    }
}

static int resolve_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    // Results were pushed by on_dns_resolved before resume; return them.
    return lua_gettop(L) >= 2 && lua_isnil(L, -2) ? 2 : 1;
}

// hv.dns.resolve(host)
static int l_dns_resolve(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    hloop_t* loop = hvlua_loop(L);

    DnsCtx* d = new DnsCtx();
    d->co = hvlua_suspend(L);

    hdns_t* q = hdns_resolve(loop, host, on_dns_resolved, d);
    if (q == NULL) {
        // Immediate failure before yielding: release the token and return
        // nil,err inline (the coroutine keeps running, no yield happened).
        hvlua_cancel(d->co);
        delete d;
        lua_pushnil(L);
        lua_pushstring(L, "dns resolve: failed to start query");
        return 2;
    }

    return lua_yieldk(L, 0, (lua_KContext)0, resolve_k);
}

static const luaL_Reg dns_funcs[] = {
    { "resolve", l_dns_resolve },
    { NULL, NULL }
};

void hvlua_open_dns(lua_State* L) {
    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_newlib(L, dns_funcs);
    lua_setfield(L, -2, "dns");
    lua_setglobal(L, "hv");
}

} // namespace hv
