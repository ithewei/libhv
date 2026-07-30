#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "hvlua.h"

#include "hbase.h"   // HV_ALLOC / HV_FREE
#include "hlog.h"

// ---------------------------------------------------------------------------
// coroutine scheduler
// ---------------------------------------------------------------------------
//
// A suspended coroutine is kept alive by an int ref into the registry (the
// lua_State* thread object). The token stores that ref plus a valid flag so a
// stale resume (loop teardown, double resume) is a safe no-op.
typedef struct HvLuaStateCtx HvLuaStateCtx;

struct HvLuaCoroutine {
    lua_State* L;      // the coroutine thread
    int        ref;    // luaL_ref of the thread in the registry, LUA_NOREF if freed
    HvLuaStateCtx* owner;
    struct HvLuaCoroutine* prev;
    struct HvLuaCoroutine* next;
};

// A "task" is a top-level coroutine with a C completion callback. The task
// struct is stored in the registry keyed by the coroutine pointer (lua_rawsetp)
// so whichever resume finishes it (initial run or a later async resume) can
// fire on_done once, without needing a C++ container.
typedef struct HvLuaTask {
    hvlua_done_cb on_done;
    void*         ud;
    int           thread_ref;  // keeps the coroutine alive between resumes
    HvLuaStateCtx* owner;
    struct HvLuaTask* prev;
    struct HvLuaTask* next;
} HvLuaTask;

struct HvLuaCleanup {
    hvlua_cleanup_cb cb;
    void* userdata;
    HvLuaStateCtx* owner;
    struct HvLuaCleanup* prev;
    struct HvLuaCleanup* next;
};

struct HvLuaStateCtx {
    HvLuaCoroutine* coroutines;
    HvLuaTask* tasks;
    HvLuaCleanup* cleanups;
    int closing;
};

static char s_state_ctx_key;

static HvLuaStateCtx* state_ctx(lua_State* L) {
    HvLuaStateCtx* ctx;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &s_state_ctx_key);
    ctx = (HvLuaStateCtx*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

static void coroutine_link(HvLuaStateCtx* ctx, HvLuaCoroutine* co) {
    co->owner = ctx;
    co->prev = NULL;
    co->next = ctx->coroutines;
    if (co->next) co->next->prev = co;
    ctx->coroutines = co;
}

static void coroutine_unlink(HvLuaCoroutine* co) {
    HvLuaStateCtx* ctx = co->owner;
    if (ctx == NULL) return;
    if (co->prev) co->prev->next = co->next;
    else ctx->coroutines = co->next;
    if (co->next) co->next->prev = co->prev;
    co->owner = NULL;
}

static void task_link(HvLuaStateCtx* ctx, HvLuaTask* task) {
    task->owner = ctx;
    task->prev = NULL;
    task->next = ctx->tasks;
    if (task->next) task->next->prev = task;
    ctx->tasks = task;
}

static void task_unlink(HvLuaTask* task) {
    HvLuaStateCtx* ctx = task->owner;
    if (ctx == NULL) return;
    if (task->prev) task->prev->next = task->next;
    else ctx->tasks = task->next;
    if (task->next) task->next->prev = task->prev;
    task->owner = NULL;
}

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
// @return 1 if the task finished (on_done fired, thread ref released), 0 if it
// yielded again. The caller must NOT touch `co` after a return of 1: finishing
// released the thread ref, so `co` may be collected — do not re-read it.
static int hvlua_task_step(lua_State* co, int nresults) {
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
        return 0;  // suspended again; a resume token owns the next wakeup
    }
    // Finished (LUA_OK) or errored: fire the completion callback, then release.
    task = task_get(co);
    if (task == NULL) return 1;
    task_set(co, NULL);  // erase before callback so a re-entrant step no-ops
    if (task->on_done) task->on_done(task->ud, status == LUA_OK, co);
    task_unlink(task);
    luaL_unref(co, LUA_REGISTRYINDEX, task->thread_ref);
    HV_FREE(task);
    return 1;
}

int hvlua_start_task(lua_State* L, int nargs, hvlua_done_cb on_done, void* ud) {
    lua_State* co;
    int thread_ref;
    HvLuaTask* task;
    int finished;
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
    task_link(state_ctx(L), task);
    task_set(co, task);

    // Use the return value instead of re-reading `co`: if it finished
    // synchronously, hvlua_task_step already released the thread ref, so
    // touching `co` again (e.g. task_get(co)) would read a coroutine whose last
    // reference is gone.
    finished = hvlua_task_step(co, nargs);
    return finished;
}

HvLuaCoroutine* hvlua_suspend(lua_State* L) {
    // L is the running coroutine. Keep it alive by ref'ing the thread object
    // in the registry so it survives GC while suspended.
    HvLuaCoroutine* co;
    HV_ALLOC_SIZEOF(co);
    co->L = L;
    lua_pushthread(L);              // push the running thread onto its own stack
    co->ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops it, stores ref in registry
    coroutine_link(state_ctx(L), co);
    return co;
}

lua_State* hvlua_coroutine_state(HvLuaCoroutine* co) {
    if (co == NULL || co->ref == LUA_NOREF) return NULL;
    return co->L;
}

void hvlua_cancel(HvLuaCoroutine* co) {
    if (co == NULL) return;
    if (co->ref != LUA_NOREF && (co->owner == NULL || !co->owner->closing)) {
        luaL_unref(co->L, LUA_REGISTRYINDEX, co->ref);
        co->ref = LUA_NOREF;
    }
    coroutine_unlink(co);
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
    coroutine_unlink(co);
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

HvLuaCleanup* hvlua_cleanup_add(lua_State* L, hvlua_cleanup_cb cb, void* userdata) {
    HvLuaStateCtx* ctx = state_ctx(L);
    HvLuaCleanup* cleanup;
    if (ctx == NULL || cb == NULL) return NULL;
    HV_ALLOC_SIZEOF(cleanup);
    cleanup->cb = cb;
    cleanup->userdata = userdata;
    cleanup->owner = ctx;
    cleanup->prev = NULL;
    cleanup->next = ctx->cleanups;
    if (cleanup->next) cleanup->next->prev = cleanup;
    ctx->cleanups = cleanup;
    return cleanup;
}

void hvlua_cleanup_del(HvLuaCleanup* cleanup) {
    HvLuaStateCtx* ctx;
    if (cleanup == NULL) return;
    ctx = cleanup->owner;
    if (ctx) {
        if (cleanup->prev) cleanup->prev->next = cleanup->next;
        else ctx->cleanups = cleanup->next;
        if (cleanup->next) cleanup->next->prev = cleanup->prev;
    }
    HV_FREE(cleanup);
}

// ---------------------------------------------------------------------------
// per-loop lua_State
// ---------------------------------------------------------------------------

static void hvlua_state_dtor(void* L) {
    lua_State* state = (lua_State*)L;
    HvLuaStateCtx* ctx;
    if (state == NULL) return;
    ctx = state_ctx(state);
    if (ctx == NULL) {
        lua_close(state);
        return;
    }
    ctx->closing = 1;
    while (ctx->cleanups) {
        HvLuaCleanup* cleanup = ctx->cleanups;
        hvlua_cleanup_cb cb = cleanup->cb;
        void* userdata = cleanup->userdata;
        ctx->cleanups = cleanup->next;
        if (ctx->cleanups) ctx->cleanups->prev = NULL;
        cleanup->owner = NULL;
        HV_FREE(cleanup);
        cb(userdata);
    }
    while (ctx->tasks) {
        HvLuaTask* task = ctx->tasks;
        ctx->tasks = task->next;
        if (task->on_done) task->on_done(task->ud, false, NULL);
        task->owner = NULL;
        HV_FREE(task);
    }
    lua_close(state);
    while (ctx->coroutines) {
        HvLuaCoroutine* co = ctx->coroutines;
        ctx->coroutines = co->next;
        HV_FREE(co);
    }
    HV_FREE(ctx);
}

static lua_State* hvlua_new_state(hloop_t* loop) {
    lua_State* L = luaL_newstate();
    HvLuaStateCtx* ctx;
    if (L == NULL) return NULL;
    HV_ALLOC_SIZEOF(ctx);
    if (ctx == NULL) {
        lua_close(L);
        return NULL;
    }
    lua_pushlightuserdata(L, ctx);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &s_state_ctx_key);
    luaL_openlibs(L);

    // Stash the owning hloop_t* in the registry so bindings can reach the loop.
    lua_pushlightuserdata(L, (void*)loop);
    lua_setfield(L, LUA_REGISTRYINDEX, "hv.loop");

    // Register modules from most basic to higher-level (mirrors libhv layering):
    //   base -> event -> json -> http/ws -> redis -> mqtt
    hvlua_open_base(L);
    hvlua_open_event(L);
    hvlua_open_json(L);
#ifdef HVLUA_WITH_HTTP
    hvlua_open_http(L);
    hvlua_open_ws(L);
#endif
#ifdef HVLUA_WITH_REDIS
    hvlua_open_redis(L);
#endif
#ifdef HVLUA_WITH_MQTT
    hvlua_open_mqtt(L);
#endif

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
