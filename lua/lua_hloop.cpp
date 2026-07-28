#include "hv_lua.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hloop.h"
#include "hlog.h"

namespace hv {

// A Lua timer: holds a ref to the callback function and the htimer_t.
// For setInterval the ref persists across fires; for setTimeout it is released
// after the single fire. The callback runs inside a fresh coroutine so it may
// itself use synchronous-style async APIs (sleep, dns, ...).
//
// Re-entrancy: the callback may call hloop.clearTimer(self). To avoid a
// use-after-free, clearTimer during the callback only marks `dead` (and deletes
// the htimer_t); on_lua_timer performs the deferred free after the callback.
struct LuaTimer {
    lua_State* L;      // per-loop main state
    htimer_t*  timer;
    int        fn_ref; // LUA_NOREF when released
    bool       once;
    bool       in_callback;
    bool       dead;   // clearTimer requested during the callback
};

static void lua_timer_release_ref(LuaTimer* lt) {
    if (lt->fn_ref != LUA_NOREF) {
        luaL_unref(lt->L, LUA_REGISTRYINDEX, lt->fn_ref);
        lt->fn_ref = LUA_NOREF;
    }
}

static void lua_timer_free(LuaTimer* lt) {
    lua_timer_release_ref(lt);
    delete lt;
}

static void on_lua_timer(htimer_t* timer) {
    LuaTimer* lt = (LuaTimer*)hevent_userdata(timer);
    if (lt == NULL || lt->fn_ref == LUA_NOREF) return;
    lua_State* L = lt->L;

    // Run the callback in a fresh coroutine so it may yield on async ops.
    lt->in_callback = true;
    lua_State* co = lua_newthread(L);
    int thread_ref = luaL_ref(L, LUA_REGISTRYINDEX); // keep coroutine alive
    lua_rawgeti(co, LUA_REGISTRYINDEX, lt->fn_ref);  // push callback fn onto co

    int nres = 0;
    int status = lua_resume(co, NULL, 0
#if LUA_VERSION_NUM >= 504
        , &nres
#endif
    );
    if (status != LUA_OK && status != LUA_YIELD) {
        const char* msg = lua_tostring(co, -1);
        hloge("[lua] timer callback error: %s", msg ? msg : "unknown");
    }
    luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
    lt->in_callback = false;

    // The callback may have cleared this timer (dead) — free it now. Otherwise
    // a once-timer (repeat==1, auto-deleted by hloop after this fire) frees here.
    if (lt->dead || lt->once) {
        lua_timer_free(lt);
    }
}

static htimer_t* add_lua_timer(lua_State* L, uint32_t timeout_ms, uint32_t repeat, bool once) {
    hloop_t* loop = hvlua_loop(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    LuaTimer* lt = new LuaTimer();
    lt->L = L;
    lt->timer = NULL;
    lt->once = once;
    lt->in_callback = false;
    lt->dead = false;
    // ref the callback function (arg 2)
    lua_pushvalue(L, 2);
    lt->fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    htimer_t* timer = htimer_add(loop, on_lua_timer, timeout_ms, repeat);
    if (timer == NULL) {
        lua_timer_free(lt);
        return NULL;
    }
    hevent_set_userdata(timer, lt);
    lt->timer = timer;
    return timer;
}

// hloop.setTimeout(ms, fn) -> lightuserdata handle
static int l_hloop_setTimeout(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    htimer_t* timer = add_lua_timer(L, ms, 1, true);
    if (timer == NULL) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, timer);
    return 1;
}

// hloop.setInterval(ms, fn) -> lightuserdata handle
static int l_hloop_setInterval(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    htimer_t* timer = add_lua_timer(L, ms, INFINITE, false);
    if (timer == NULL) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, timer);
    return 1;
}

// hloop.clearTimer(handle)
static int l_hloop_clearTimer(lua_State* L) {
    if (!lua_islightuserdata(L, 1)) return 0;
    htimer_t* timer = (htimer_t*)lua_touserdata(L, 1);
    if (timer == NULL) return 0;
    LuaTimer* lt = (LuaTimer*)hevent_userdata(timer);
    htimer_del(timer);
    if (lt) {
        if (lt->in_callback) {
            // Called from within this timer's own callback: defer the free to
            // on_lua_timer so we don't free `lt` while it's still in use.
            lt->dead = true;
            lua_timer_release_ref(lt);
        } else {
            lua_timer_free(lt);
        }
    }
    return 0;
}

// ---- hloop.sleep(ms): suspend the running coroutine for ms milliseconds ----

struct SleepCtx {
    HvLuaCoroutine* co;
    htimer_t*       timer;
};

static void on_sleep_timer(htimer_t* timer) {
    SleepCtx* s = (SleepCtx*)hevent_userdata(timer);
    HvLuaCoroutine* co = s->co;
    delete s;
    // resume the sleeping coroutine with no results
    hvlua_resume(co, 0);
}

// continuation: nothing to return after wakeup
static int sleep_k(lua_State* L, int status, lua_KContext ctx) {
    (void)L; (void)status; (void)ctx;
    return 0;
}

// hloop.sleep(ms)
static int l_hloop_sleep(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    hloop_t* loop = hvlua_loop(L);

    SleepCtx* s = new SleepCtx();
    s->co = hvlua_suspend(L);
    s->timer = htimer_add(loop, on_sleep_timer, ms, 1);
    hevent_set_userdata(s->timer, s);

    return lua_yieldk(L, 0, (lua_KContext)0, sleep_k);
}

// hloop.run() / hloop.stop()
static int l_hloop_run(lua_State* L) {
    hloop_run(hvlua_loop(L));
    return 0;
}

static int l_hloop_stop(lua_State* L) {
    hloop_stop(hvlua_loop(L));
    return 0;
}

static const luaL_Reg hloop_funcs[] = {
    { "setTimeout",  l_hloop_setTimeout  },
    { "setInterval", l_hloop_setInterval },
    { "clearTimer",  l_hloop_clearTimer  },
    { "sleep",       l_hloop_sleep       },
    { "run",         l_hloop_run         },
    { "stop",        l_hloop_stop        },
    { NULL, NULL }
};

void hvlua_open_hloop(lua_State* L) {
    luaL_newlib(L, hloop_funcs);
    lua_setglobal(L, "hloop");
}

} // namespace hv
