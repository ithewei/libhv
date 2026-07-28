#include "hv_lua.h"

#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hlog.h"

// Module registration entry points implemented in the per-module files.
namespace hv {
void hvlua_open_hloop(lua_State* L);   // lua_hloop.cpp  -> global "hloop"
void hvlua_open_core(lua_State* L);    // lua_hv_core.cpp -> table "hv"
void hvlua_open_dns(lua_State* L);     // lua_hv_dns.cpp  -> hv.dns
}

namespace hv {

// ---------------------------------------------------------------------------
// coroutine scheduler
// ---------------------------------------------------------------------------
//
// A suspended coroutine is kept alive by an int ref into the registry (the
// lua_State* thread object). The token stores that ref plus a generation/valid
// flag so a stale resume (loop teardown, double resume) is a safe no-op.
struct HvLuaCoroutine {
    lua_State* main;   // the per-loop main state (owns the registry)
    lua_State* L;      // the coroutine thread
    int        ref;    // luaL_ref of the thread in the registry, LUA_NOREF if freed
};

HvLuaCoroutine* hvlua_suspend(lua_State* L) {
    // L is the running coroutine. Keep it alive by ref'ing the thread object
    // in the registry so it survives GC while suspended.
    HvLuaCoroutine* co = new HvLuaCoroutine();
    co->main = L;
    co->L = L;
    lua_pushthread(L);              // push the running thread onto its own stack
    co->ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops it, stores ref in registry
    return co;
}

lua_State* hvlua_coroutine_state(HvLuaCoroutine* co) {
    if (co == NULL || co->ref == LUA_NOREF) return NULL;
    return co->L;
}

void hvlua_cancel(HvLuaCoroutine* co) {
    if (co == NULL) return;
    if (co->ref != LUA_NOREF) {
        luaL_unref(co->L, LUA_REGISTRYINDEX, co->ref);
        co->ref = LUA_NOREF;
    }
    delete co;
}

void hvlua_resume(HvLuaCoroutine* co, int nresults) {
    if (co == NULL) return;
    if (co->ref == LUA_NOREF) {  // stale: already resumed/freed
        delete co;
        return;
    }
    lua_State* L = co->L;
    int ref = co->ref;
    // Release the registry ref BEFORE resuming so a re-entrant resume can't
    // double-free, and so the thread can be GC'd once it finishes.
    co->ref = LUA_NOREF;

    int status = lua_resume(L, NULL, nresults
#if LUA_VERSION_NUM >= 504
        , &nresults
#endif
    );
    if (status != LUA_OK && status != LUA_YIELD) {
        const char* msg = lua_tostring(L, -1);
        hloge("[lua] coroutine error: %s", msg ? msg : "unknown");
    }
    // Unref the thread now that it has either finished or yielded again.
    // (If it yielded again, a new token was created via hvlua_suspend.)
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    delete co;
}

// ---------------------------------------------------------------------------
// per-loop lua_State
// ---------------------------------------------------------------------------

static void hvlua_state_dtor(void* L) {
    if (L) lua_close((lua_State*)L);
}

static lua_State* hvlua_new_state(hloop_t* loop) {
    lua_State* L = luaL_newstate();
    if (L == NULL) return NULL;
    luaL_openlibs(L);

    // Stash the owning hloop_t* in the registry so bindings can reach the loop.
    lua_pushlightuserdata(L, (void*)loop);
    lua_setfield(L, LUA_REGISTRYINDEX, "hv.loop");

    hvlua_open_hloop(L);
    hvlua_open_core(L);
    hvlua_open_dns(L);

    hloop_set_lua_state(loop, L, hvlua_state_dtor);
    return L;
}

lua_State* hvlua_state(hloop_t* loop) {
    if (loop == NULL) return NULL;
    lua_State* L = (lua_State*)hloop_lua_state(loop);
    if (L == NULL) {
        L = hvlua_new_state(loop);
    }
    return L;
}

// Recover the owning loop for a state (used by bindings).
hloop_t* hvlua_loop(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "hv.loop");
    hloop_t* loop = (hloop_t*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return loop;
}

// ---------------------------------------------------------------------------
// run helpers
// ---------------------------------------------------------------------------

// Run a loaded chunk (on stack top of main L) inside a fresh coroutine.
static int hvlua_run_chunk(lua_State* L) {
    // stack: [chunk]
    lua_State* co = lua_newthread(L);   // stack: [chunk][thread]
    lua_pushvalue(L, -2);               // stack: [chunk][thread][chunk]
    lua_xmove(L, co, 1);                // move chunk into co; stack: [chunk][thread]

    // Keep the thread referenced while it may yield.
    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops thread; stack: [chunk]
    lua_pop(L, 1);                      // pop chunk; stack: []

    int nres = 0;
    int status = lua_resume(co, NULL, 0
#if LUA_VERSION_NUM >= 504
        , &nres
#endif
    );
    if (status == LUA_YIELD) {
        // Suspended on an async op; a resume token (from hvlua_suspend) now
        // owns its own ref. Release ours; the coroutine stays alive via that.
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return 0;
    }
    if (status != LUA_OK) {
        const char* msg = lua_tostring(co, -1);
        hloge("[lua] script error: %s", msg ? msg : "unknown");
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return -1;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    return 0;
}

int hvlua_dofile(hloop_t* loop, const char* filepath) {
    lua_State* L = hvlua_state(loop);
    if (L == NULL) return -1;
    if (luaL_loadfile(L, filepath) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        hloge("[lua] load %s failed: %s", filepath, msg ? msg : "unknown");
        lua_pop(L, 1);
        return -1;
    }
    return hvlua_run_chunk(L);
}

int hvlua_dostring(hloop_t* loop, const char* code) {
    lua_State* L = hvlua_state(loop);
    if (L == NULL) return -1;
    if (luaL_loadstring(L, code) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        hloge("[lua] load string failed: %s", msg ? msg : "unknown");
        lua_pop(L, 1);
        return -1;
    }
    return hvlua_run_chunk(L);
}

} // namespace hv
