#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "hv_lua.h"

#include "hbase.h"   // HV_ALLOC / HV_FREE
#include "hlog.h"

// ---------------------------------------------------------------------------
// coroutine scheduler
// ---------------------------------------------------------------------------
//
// A suspended coroutine is kept alive by an int ref into the registry (the
// lua_State* thread object). The token stores that ref plus a valid flag so a
// stale resume (loop teardown, double resume) is a safe no-op.
struct HvLuaCoroutine {
    lua_State* L;      // the coroutine thread
    int        ref;    // luaL_ref of the thread in the registry, LUA_NOREF if freed
};

// A "task" is a top-level coroutine with a C completion callback. The task
// struct is stored in the registry keyed by the coroutine pointer (lua_rawsetp)
// so whichever resume finishes it (initial run or a later async resume) can
// fire on_done once, without needing a C++ container.
typedef struct HvLuaTask {
    hvlua_done_cb on_done;
    void*         ud;
    int           thread_ref;  // keeps the coroutine alive between resumes
} HvLuaTask;

// registry[co] = lightuserdata(task)   (NULL entry == not a task / finished)
static HvLuaTask* task_get(lua_State* co) {
    HvLuaTask* task;
    lua_rawgetp(co, LUA_REGISTRYINDEX, co);
    task = (HvLuaTask*)lua_touserdata(co, -1);
    lua_pop(co, 1);
    return task;
}

static void task_set(lua_State* co, HvLuaTask* task) {
    if (task) {
        lua_pushlightuserdata(co, task);
    } else {
        lua_pushnil(co);
    }
    lua_rawsetp(co, LUA_REGISTRYINDEX, co);
}

// Drive one resume step of a task coroutine. Fires on_done when it finishes.
// `co` is the coroutine; nresults is the number of values already pushed on it.
static void hvlua_task_step(lua_State* co, int nresults) {
    HvLuaTask* task;
    int nres = 0;
    int status;
    (void)nres;
    status = lua_resume(co, NULL, nresults
#if LUA_VERSION_NUM >= 504
        , &nres
#endif
    );
    if (status == LUA_YIELD) {
        return;  // suspended again; a resume token owns the next wakeup
    }
    // Finished (LUA_OK) or errored: fire the completion callback, then release.
    task = task_get(co);
    if (task == NULL) return;
    task_set(co, NULL);  // erase before callback so a re-entrant step no-ops
    if (task->on_done) task->on_done(task->ud, status == LUA_OK, co);
    luaL_unref(co, LUA_REGISTRYINDEX, task->thread_ref);
    HV_FREE(task);
}

int hvlua_start_task(lua_State* L, int nargs, hvlua_done_cb on_done, void* ud) {
    lua_State* co;
    int thread_ref;
    HvLuaTask* task;
    // stack (top): fn, arg1, ..., argN
    co = lua_newthread(L);                 // push thread
    // move fn+args (below the thread) into the coroutine
    lua_insert(L, -(nargs + 2));           // move thread below fn+args
    lua_xmove(L, co, nargs + 1);           // move fn+args into co; thread stays on L
    thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pop+ref the thread

    HV_ALLOC_SIZEOF(task);
    task->on_done = on_done;
    task->ud = ud;
    task->thread_ref = thread_ref;
    task_set(co, task);

    hvlua_task_step(co, nargs);
    // If the task is no longer tracked, it finished synchronously.
    return task_get(co) == NULL ? 1 : 0;
}

HvLuaCoroutine* hvlua_suspend(lua_State* L) {
    // L is the running coroutine. Keep it alive by ref'ing the thread object
    // in the registry so it survives GC while suspended.
    HvLuaCoroutine* co;
    HV_ALLOC_SIZEOF(co);
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
    HV_FREE(co);
}

void hvlua_resume(HvLuaCoroutine* co, int nresults) {
    lua_State* L;
    int ref;
    if (co == NULL) return;
    if (co->ref == LUA_NOREF) {  // stale: already resumed/freed
        HV_FREE(co);
        return;
    }
    L = co->L;
    ref = co->ref;
    // Release the suspend token's registry ref BEFORE resuming so a re-entrant
    // resume can't double-free. Task tracking keeps its own ref, so the
    // coroutine stays alive across this transition.
    co->ref = LUA_NOREF;
    HV_FREE(co);

    // Drive the coroutine; hvlua_task_step fires on_done if it finishes and is
    // a tracked task. For non-task coroutines it just resumes/logs errors.
    if (task_get(L) != NULL) {
        hvlua_task_step(L, nresults);
    } else {
        int nres = 0;
        int status;
        (void)nres;
        status = lua_resume(L, NULL, nresults
#if LUA_VERSION_NUM >= 504
            , &nres
#endif
        );
        if (status != LUA_OK && status != LUA_YIELD) {
            const char* msg = lua_tostring(L, -1);
            hloge("[lua] coroutine error: %s", msg ? msg : "unknown");
        }
    }
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
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
    lua_State* L;
    if (loop == NULL) return NULL;
    L = (lua_State*)hloop_lua_state(loop);
    if (L == NULL) {
        L = hvlua_new_state(loop);
    }
    return L;
}

// Recover the owning loop for a state (used by bindings).
hloop_t* hvlua_loop(lua_State* L) {
    hloop_t* loop;
    lua_getfield(L, LUA_REGISTRYINDEX, "hv.loop");
    loop = (hloop_t*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return loop;
}

// ---------------------------------------------------------------------------
// run helpers
// ---------------------------------------------------------------------------

// Run a loaded chunk (on stack top of main L) inside a fresh coroutine.
static int hvlua_run_chunk(lua_State* L) {
    lua_State* co;
    int ref;
    int nres = 0;
    int status;
    (void)nres;
    // stack: [chunk]
    co = lua_newthread(L);              // stack: [chunk][thread]
    lua_pushvalue(L, -2);               // stack: [chunk][thread][chunk]
    lua_xmove(L, co, 1);                // move chunk into co; stack: [chunk][thread]

    // Keep the thread referenced while it may yield.
    ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops thread; stack: [chunk]
    lua_pop(L, 1);                      // pop chunk; stack: []

    status = lua_resume(co, NULL, 0
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
