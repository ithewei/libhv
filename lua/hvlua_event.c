#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "hvlua.h"

#include <string.h>   // memset / memcpy / strcmp

#include "hbase.h"   // HV_ALLOC / HV_FREE
#include "hevent.h"  // MAX_READ_BUFSIZE
#include "hloop.h"
#include "hlog.h"
#include "hdns.h"     // async DNS is part of the event/ layer (hv.resolveDns)
#include "hsocket.h"  // sockaddr_ip

// A Lua timer: holds a ref to the callback function and the htimer_t.
// For setInterval the ref persists across fires; for setTimeout it is released
// after the single fire. The callback runs inside a fresh coroutine so it may
// itself use synchronous-style async APIs (sleep, dns, ...).
//
// Re-entrancy: the callback may call hv.clearTimer(self). To avoid a
// use-after-free, clearTimer during the callback only marks `dead` (and deletes
// the htimer_t); on_lua_timer performs the deferred free after the callback.
typedef struct LuaTimer {
    lua_State* L;      // per-loop main state
    htimer_t*  timer;
    HvLuaCleanup* cleanup;
    int        fn_ref; // LUA_NOREF when released
    int        once;
    int        in_callback;
    int        dead;   // clearTimer requested during the callback
} LuaTimer;

static void lua_timer_release_ref(LuaTimer* lt) {
    if (lt->fn_ref != LUA_NOREF) {
        luaL_unref(lt->L, LUA_REGISTRYINDEX, lt->fn_ref);
        lt->fn_ref = LUA_NOREF;
    }
}

// Live-timer registry: registry["hv.timers"][lightuserdata(timer)] = true.
// hv.clearTimer receives a raw htimer_t* as an opaque handle, but a fired
// once-timer's htimer_t is freed by the loop after its callback, leaving the
// script holding a stale pointer. Validating the handle against this registry
// (instead of blindly dereferencing hevent_userdata) makes clearTimer on a
// stale/already-freed handle a safe no-op.
#define HVLUA_TIMERS_REG "hv.timers"

static void lua_timers_reg_get(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, HVLUA_TIMERS_REG);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, HVLUA_TIMERS_REG);
    }
}

static void lua_timer_reg_add(lua_State* L, htimer_t* timer) {
    lua_timers_reg_get(L);              // [timers]
    lua_pushlightuserdata(L, timer);    // [timers][key]
    lua_pushboolean(L, 1);              // [timers][key][true]
    lua_settable(L, -3);                // timers[key]=true
    lua_pop(L, 1);
}

static void lua_timer_reg_remove(lua_State* L, htimer_t* timer) {
    if (timer == NULL) return;
    lua_timers_reg_get(L);              // [timers]
    lua_pushlightuserdata(L, timer);    // [timers][key]
    lua_pushnil(L);                     // [timers][key][nil]
    lua_settable(L, -3);                // timers[key]=nil
    lua_pop(L, 1);
}

// Is `timer` a currently-live lua timer handle?
static int lua_timer_reg_has(lua_State* L, htimer_t* timer) {
    int has;
    lua_timers_reg_get(L);              // [timers]
    lua_pushlightuserdata(L, timer);    // [timers][key]
    lua_gettable(L, -2);                // [timers][val]
    has = lua_toboolean(L, -1);
    lua_pop(L, 2);
    return has;
}

static void lua_timer_free(LuaTimer* lt) {
    hvlua_cleanup_del(lt->cleanup);
    lt->cleanup = NULL;
    // Unregister the handle so a later clearTimer on this (soon-freed) timer is
    // a safe no-op, and detach the timer's back-pointer to lt.
    if (lt->timer) {
        lua_timer_reg_remove(lt->L, lt->timer);
        hevent_set_userdata(lt->timer, NULL);
    }
    lua_timer_release_ref(lt);
    HV_FREE(lt);
}

static void lua_timer_cleanup(void* userdata) {
    LuaTimer* lt = (LuaTimer*)userdata;
    lt->cleanup = NULL;
    lua_timer_free(lt);
}

static void on_lua_timer(htimer_t* timer) {
    LuaTimer* lt = (LuaTimer*)hevent_userdata(timer);
    lua_State* L;
    lua_State* co;
    int thread_ref;
    int nres = 0;
    int status;
    if (lt == NULL || lt->fn_ref == LUA_NOREF) return;
    L = lt->L;
    (void)nres;

    // Run the callback in a fresh coroutine so it may yield on async ops.
    lt->in_callback = 1;
    co = lua_newthread(L);
    thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);     // keep coroutine alive
    lua_rawgeti(co, LUA_REGISTRYINDEX, lt->fn_ref);  // push callback fn onto co

    status = lua_resume(co, NULL, 0
#if LUA_VERSION_NUM >= 504
        , &nres
#endif
    );
    if (status != LUA_OK && status != LUA_YIELD) {
        const char* msg = lua_tostring(co, -1);
        hloge("[lua] timer callback error: %s", msg ? msg : "unknown");
    }
    luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
    lt->in_callback = 0;

    // The callback may have cleared this timer (dead) — free it now. Otherwise
    // a once-timer (repeat==1, auto-deleted by hloop after this fire) frees here.
    if (lt->dead || lt->once) {
        lua_timer_free(lt);
    }
}

static htimer_t* add_lua_timer(lua_State* L, uint32_t timeout_ms, uint32_t repeat, int once) {
    hloop_t* loop = hvlua_loop(L);
    LuaTimer* lt;
    htimer_t* timer;
    luaL_checktype(L, 2, LUA_TFUNCTION);

    HV_ALLOC_SIZEOF(lt);
    // Store the per-loop MAIN lua_State, not the calling coroutine L: on_lua_timer
    // later does lua_newthread(lt->L), and the calling coroutine may be collected
    // before the timer fires (e.g. a timer registered inside another timer's
    // callback — that callback coroutine is unref'd right after it returns),
    // which would make lt->L a dangling pointer (UAF). The main state lives as
    // long as the loop. (LUA_REGISTRYINDEX is shared across all threads of the
    // state, so the fn ref below is valid regardless of which thread refs it.)
    // Mirrors on_server_accept / on_udp_server_read which use hloop_lua_state().
    lt->L = (lua_State*)hloop_lua_state(loop);
    lt->timer = NULL;
    lt->cleanup = NULL;
    lt->once = once;
    lt->in_callback = 0;
    lt->dead = 0;
    // ref the callback function (arg 2)
    lua_pushvalue(L, 2);
    lt->fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    timer = htimer_add(loop, on_lua_timer, timeout_ms, repeat);
    if (timer == NULL) {
        lua_timer_free(lt);
        return NULL;
    }
    hevent_set_userdata(timer, lt);
    lt->timer = timer;
    lt->cleanup = hvlua_cleanup_add(L, lua_timer_cleanup, lt);
    lua_timer_reg_add(L, timer);   // mark handle live for clearTimer validation
    return timer;
}

// hv.setTimeout(ms, fn) -> lightuserdata handle
static int l_hloop_setTimeout(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    htimer_t* timer = add_lua_timer(L, ms, 1, 1);
    if (timer == NULL) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, timer);
    return 1;
}

// hv.setInterval(ms, fn) -> lightuserdata handle
static int l_hloop_setInterval(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    htimer_t* timer = add_lua_timer(L, ms, INFINITE, 0);
    if (timer == NULL) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, timer);
    return 1;
}

// hv.clearTimer(handle)
static int l_hloop_clearTimer(lua_State* L) {
    htimer_t* timer;
    LuaTimer* lt;
    if (!lua_islightuserdata(L, 1)) return 0;
    timer = (htimer_t*)lua_touserdata(L, 1);
    if (timer == NULL) return 0;
    // Validate the handle: a fired once-timer's htimer_t is freed by the loop,
    // so the script may hold a stale pointer. Only proceed if the handle is a
    // currently-live timer we registered; otherwise it's a safe no-op (avoids
    // dereferencing freed memory via hevent_userdata).
    if (!lua_timer_reg_has(L, timer)) return 0;
    lt = (LuaTimer*)hevent_userdata(timer);
    htimer_del(timer);
    if (lt) {
        if (lt->in_callback) {
            // Called from within this timer's own callback: defer the free to
            // on_lua_timer so we don't free `lt` while it's still in use. Drop it
            // from the live registry now so no further clearTimer can match.
            lt->dead = 1;
            lua_timer_reg_remove(L, timer);
            lua_timer_release_ref(lt);
        } else {
            lua_timer_free(lt);
        }
    }
    return 0;
}

// ---- hv.sleep(ms): suspend the running coroutine for ms milliseconds ----

typedef struct SleepCtx {
    HvLuaCoroutine* co;
    htimer_t*       timer;
    HvLuaCleanup*   cleanup;
} SleepCtx;

static void sleep_cleanup(void* userdata) {
    SleepCtx* s = (SleepCtx*)userdata;
    s->cleanup = NULL;
    if (s->timer) hevent_set_userdata(s->timer, NULL);
    hvlua_cancel(s->co);
    HV_FREE(s);
}

static void on_sleep_timer(htimer_t* timer) {
    SleepCtx* s = (SleepCtx*)hevent_userdata(timer);
    HvLuaCoroutine* co = s->co;
    hvlua_cleanup_del(s->cleanup);
    s->cleanup = NULL;
    HV_FREE(s);
    // resume the sleeping coroutine with no results
    hvlua_resume(co, 0);
}

// continuation: nothing to return after wakeup
static int sleep_k(lua_State* L, int status, lua_KContext ctx) {
    (void)L; (void)status; (void)ctx;
    return 0;
}

// hv.sleep(ms)
static int l_hloop_sleep(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    hloop_t* loop = hvlua_loop(L);
    SleepCtx* s;
    htimer_t* timer;

    // Create the timer BEFORE suspending: if it fails (NULL loop / out of
    // resources) we must not deref NULL nor leave the coroutine suspended
    // forever with nothing to wake it. Fail fast with (nil, err) instead.
    timer = htimer_add(loop, on_sleep_timer, ms, 1);
    if (timer == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.sleep: create timer failed");
        return 2;
    }

    HV_ALLOC_SIZEOF(s);
    s->co = hvlua_suspend(L);
    s->timer = timer;
    s->cleanup = hvlua_cleanup_add(L, sleep_cleanup, s);
    hevent_set_userdata(timer, s);

    return lua_yieldk(L, 0, (lua_KContext)0, sleep_k);
}

// ---- hv.resolveDns(host): coroutine-synchronous async DNS ----
//
// DNS resolution is part of the event/ layer (event/hdns.c), same as timers,
// so it lives here alongside setTimeout/sleep rather than in a hv.* module.
// Suspends the running coroutine, issues an async hdns query on the loop, and
// resumes with the resolved addresses (or nil,err) on the same loop thread.

typedef struct DnsCtx {
    HvLuaCoroutine* co;
    HvLuaCleanup* cleanup;
} DnsCtx;

static void dns_cleanup(void* userdata) {
    DnsCtx* d = (DnsCtx*)userdata;
    d->cleanup = NULL;
    hvlua_cancel(d->co);
    HV_FREE(d);
}

static void on_dns_resolved(hdns_t* query, const hdns_result_t* result, void* userdata) {
    DnsCtx* d = (DnsCtx*)userdata;
    HvLuaCoroutine* co = d->co;
    lua_State* L;
    (void)query;
    hvlua_cleanup_del(d->cleanup);
    d->cleanup = NULL;
    HV_FREE(d);

    L = hvlua_coroutine_state(co);
    if (L == NULL) {   // coroutine gone (loop teardown); release the token
        hvlua_cancel(co);
        return;
    }

    if (result->status == HDNS_STATUS_OK && result->naddrs > 0) {
        int i;
        lua_createtable(L, result->naddrs, 0);
        for (i = 0; i < result->naddrs; ++i) {
            char ip[64];
            ip[0] = '\0';
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

// hv.resolveDns(host) -> { ip, ip, ... } | nil, err
static int l_hloop_resolveDns(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    hloop_t* loop = hvlua_loop(L);
    DnsCtx* d;
    hdns_t* q;

    HV_ALLOC_SIZEOF(d);
    d->co = hvlua_suspend(L);
    d->cleanup = hvlua_cleanup_add(L, dns_cleanup, d);

    q = hdns_resolve(loop, host, on_dns_resolved, d);
    if (q == NULL) {
        // Immediate failure before yielding: release the token and return
        // nil,err inline (the coroutine keeps running, no yield happened).
        hvlua_cancel(d->co);
        hvlua_cleanup_del(d->cleanup);
        HV_FREE(d);
        lua_pushnil(L);
        lua_pushstring(L, "dns resolve: failed to start query");
        return 2;
    }

    return lua_yieldk(L, 0, (lua_KContext)0, resolve_k);
}

// hv.run() / hv.stop()
static int l_hloop_run(lua_State* L) {
    (void)L;
    return 0;
}

static void on_hloop_stop_timer(htimer_t* timer) {
    hloop_stop(hevent_loop(timer));
}

static int l_hloop_stop(lua_State* L) {
    hloop_t* loop = hvlua_loop(L);
    hloop_status_e status = hloop_status(loop);
    if (status == HLOOP_STATUS_RUNNING || status == HLOOP_STATUS_PAUSE) {
        hloop_stop(loop);
    } else {
        htimer_add(loop, on_hloop_stop_timer, 1, 1);
    }
    return 0;
}

// ============================================================================
// TCP client: hv.tcpClient(host, port [, timeout_ms]) -> conn | nil, err
//             (alias: hv.connect)
//
// conn is a userdata wrapping an hio_t that lives on the current loop (no evpp,
// no extra thread). All callbacks fire on this loop thread, so we reuse the same
// same-thread suspend/resume machinery as sleep/dns. A conn has at most one
// pending coroutine at a time (connect OR read); close resumes it with nil,err.
// ============================================================================

static const char* CONN_META = "hv.conn";

typedef struct LuaConn {
    hio_t*          io;
    HvLuaCoroutine* co;       // the coroutine waiting on connect/read (or NULL)
    int             closed;   // set in the close callback
    int             connecting; // pending op is connect (vs read), for resume shape
    unpack_setting_t* unpack; // owned; hio_t only stores the pointer
    // Sync-read detection: hio_read_until_length/delim may invoke the read
    // callback INLINE (buffered data already satisfies the request) before the
    // coroutine yields. In that window co is not yet set, so on_conn_read must
    // not resume; instead it stashes the data here and l_conn_read* returns it
    // directly without suspending.
    lua_State*      reading_L;  // non-NULL while arming a read (the running coroutine)
    int             read_done;  // set true if the read completed synchronously
    int             read_nres;  // number of results pushed on reading_L (sync path)
} LuaConn;

// forward decl: conn_push_new is defined lower (after the read helpers) but
// used by l_hv_connect above it.
static LuaConn* conn_push_new(lua_State* L, hio_t* io);

static LuaConn* lua_check_conn(lua_State* L) {
    return (LuaConn*)luaL_checkudata(L, 1, CONN_META);
}

// Resume the conn's pending coroutine (if any) with values already pushed on it.
static void conn_resume(LuaConn* c, int nresults) {
    HvLuaCoroutine* co = c->co;
    c->co = NULL;
    hvlua_resume(co, nresults);
}

static void on_conn_close(hio_t* io) {
    LuaConn* c = (LuaConn*)hevent_userdata(io);
    if (c == NULL) return;
    c->closed = 1;
    c->io = NULL;  // hio_t is freed by the loop after this returns; drop it
    if (c->co) {
        lua_State* co = hvlua_coroutine_state(c->co);
        if (co) {
            lua_pushnil(co);
            lua_pushstring(co, "closed");
            conn_resume(c, 2);
        } else {
            hvlua_cancel(c->co);
            c->co = NULL;
        }
    }
}

static void on_conn_connect(hio_t* io) {
    LuaConn* c = (LuaConn*)hevent_userdata(io);
    if (c == NULL || c->co == NULL) return;
    lua_State* co = hvlua_coroutine_state(c->co);
    if (co == NULL) { hvlua_cancel(c->co); c->co = NULL; return; }
    // resume with the conn userdata itself (kept in the registry via co ref).
    // The conn is already on the coroutine stack as the yielded self; we just
    // signal success by pushing true, and the continuation returns the conn.
    lua_pushboolean(co, 1);
    conn_resume(c, 1);
}

static void on_conn_read(hio_t* io, void* buf, int len) {
    LuaConn* c = (LuaConn*)hevent_userdata(io);
    if (c == NULL) return;
    // Synchronous completion: hio_read_until_* found buffered data and called
    // us inline, before the coroutine yielded. co is not set yet; push the data
    // onto the running coroutine and let l_conn_read* return it directly (do NOT
    // resume — the coroutine is still running).
    if (c->reading_L) {
        lua_pushlstring(c->reading_L, (const char*)buf, len);
        c->read_done = 1;
        c->read_nres = 1;
        return;
    }
    if (c->co == NULL) return;
    lua_State* co = hvlua_coroutine_state(c->co);
    if (co == NULL) { hvlua_cancel(c->co); c->co = NULL; return; }
    lua_pushlstring(co, (const char*)buf, len);
    conn_resume(c, 1);
}

// continuation for hv.connect: success resumes with a boolean `true` marker on
// top; failure (close before connect) resumes with (nil, err) already on the
// stack. Distinguish by the type at the top of the stack.
static int connect_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
        // success: replace the `true` marker with the conn userdata (self,
        // captured at stack index 1 before the yield).
        lua_pop(L, 1);
        lua_pushvalue(L, 1);
        return 1;
    }
    // failure: (nil, err) were pushed by on_conn_close; hand them back.
    return 2;
}

static int l_hv_connect(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);
    int timeout_ms = (int)luaL_optinteger(L, 3, 0);
    hloop_t* loop = hvlua_loop(L);

    hio_t* io = hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.tcpClient: create socket failed");
        return 2;
    }

    // conn userdata (becomes stack slot 1's sibling; we return it on success)
    LuaConn* c = conn_push_new(L, io);
    c->connecting = 1;
    // move the conn userdata to stack index 1 so connect_k can return it as self
    lua_replace(L, 1);

    hio_setcb_connect(io, on_conn_connect);
    if (timeout_ms > 0) hio_set_connect_timeout(io, timeout_ms);

    c->co = hvlua_suspend(L);
    hio_connect(io);
    return lua_yieldk(L, 0, (lua_KContext)0, connect_k);
}

// Push a new conn userdata wrapping an existing hio_t (used by connect + accept).
// Sets the close callback; leaves read/connect callbacks to the caller.
static LuaConn* conn_push_new(lua_State* L, hio_t* io) {
    LuaConn* c = (LuaConn*)lua_newuserdata(L, sizeof(LuaConn));
    c->io = io;
    c->co = NULL;
    c->closed = 0;
    c->connecting = 0;
    c->unpack = NULL;
    c->reading_L = NULL;
    c->read_done = 0;
    c->read_nres = 0;
    luaL_getmetatable(L, CONN_META);
    lua_setmetatable(L, -2);
    hevent_set_userdata(io, c);
    hio_setcb_close(io, on_conn_close);
    return c;
}

// conn:read* -> data | nil, err
static int read_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    return lua_gettop(L) >= 2 && lua_isnil(L, -2) ? 2 : 1;
}

// Read protocol (fixes sync-callback data loss): the hio_read_until_* calls may
// invoke on_conn_read INLINE when buffered data already satisfies the request.
// So we (1) arm the read callback and mark reading_L BEFORE issuing hio_read*,
// (2) issue hio_read*, then (3) if the callback fired synchronously (read_done),
// return the data directly; otherwise suspend the coroutine.
//
// conn_begin_read: arm callback + enter the "reading" window. Returns 0 on ok.
static void conn_begin_read(lua_State* L, LuaConn* c) {
    hio_setcb_read(c->io, on_conn_read);
    c->reading_L = L;
    c->read_done = 0;
    c->read_nres = 0;
}

// conn_end_read: leave the reading window; return results if the read completed
// synchronously, else suspend the coroutine and yield.
static int conn_end_read(lua_State* L, LuaConn* c) {
    c->reading_L = NULL;
    if (c->read_done) {
        // Data already pushed on L by on_conn_read; return it directly.
        return c->read_nres;
    }
    // Also handle the case where the read synchronously closed the connection
    // (on_conn_close ran inline and set closed): report it without suspending.
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    c->co = hvlua_suspend(L);
    return lua_yieldk(L, 0, (lua_KContext)0, read_k);
}

// conn:read() -> data (read once: whatever bytes are available)
static int l_conn_read(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    conn_begin_read(L, c);
    hio_read(c->io);
    return conn_end_read(L, c);
}

// conn:readbytes(n) -> exactly n bytes (hio_read_until_length)
static int l_conn_readbytes(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    lua_Integer value = luaL_checkinteger(L, 2);
    unsigned int n;
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    if (value <= 0 || (lua_Unsigned)value > MAX_READ_BUFSIZE) {
        return luaL_error(L, "conn:readbytes length must be between 1 and %u", MAX_READ_BUFSIZE);
    }
    n = (unsigned int)value;
    conn_begin_read(L, c);
    hio_read_until_length(c->io, n);
    return conn_end_read(L, c);
}

// conn:readuntil(delim) -> data up to and including the 1-byte delimiter
static int l_conn_readuntil(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    size_t dlen = 0;
    const char* delim = luaL_checklstring(L, 2, &dlen);
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    if (dlen != 1) {
        return luaL_error(L, "conn:readuntil expects a single-byte delimiter");
    }
    conn_begin_read(L, c);
    hio_read_until_delim(c->io, (unsigned char)delim[0]);
    return conn_end_read(L, c);
}

// conn:readline() -> data up to and including '\n'
static int l_conn_readline(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    conn_begin_read(L, c);
    hio_read_until_delim(c->io, '\n');
    return conn_end_read(L, c);
}

// conn:setUnpack(opts): configure automatic message unpacking so subsequent
// conn:read() returns one complete packet. opts is a table:
//   mode = "none"|"fixed"|"delimiter"|"length_field"
//   package_max_length = <int>
//   fixed_length = <int>                       (fixed)
//   delimiter = "<bytes>"                       (delimiter)
//   body_offset, length_field_offset,
//   length_field_bytes, length_adjustment,
//   length_field_coding = "be"|"le"|"varint"|"asn1"   (length_field)
static int l_conn_setUnpack(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    const char* mode;
    lua_Integer value;
    if (c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    luaL_checktype(L, 2, LUA_TTABLE);

    if (c->unpack == NULL) {
        HV_ALLOC_SIZEOF(c->unpack);
    }
    memset(c->unpack, 0, sizeof(*c->unpack));
    c->unpack->package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;

    lua_getfield(L, 2, "package_max_length");
    if (lua_isinteger(L, -1)) {
        value = lua_tointeger(L, -1);
        if (value <= 0 || (lua_Unsigned)value > MAX_READ_BUFSIZE) {
            return luaL_error(L, "conn:setUnpack: invalid package_max_length");
        }
        c->unpack->package_max_length = (unsigned int)value;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "mode");
    mode = luaL_optstring(L, -1, "length_field");
    lua_pop(L, 1);

    if (strcmp(mode, "none") == 0) {
        c->unpack->mode = UNPACK_MODE_NONE;
    } else if (strcmp(mode, "fixed") == 0) {
        c->unpack->mode = UNPACK_BY_FIXED_LENGTH;
        lua_getfield(L, 2, "fixed_length");
        value = luaL_optinteger(L, -1, 0);
        if (value <= 0 || (lua_Unsigned)value > c->unpack->package_max_length) {
            return luaL_error(L, "conn:setUnpack: invalid fixed_length");
        }
        c->unpack->fixed_length = (unsigned int)value;
        lua_pop(L, 1);
    } else if (strcmp(mode, "delimiter") == 0) {
        size_t dlen = 0;
        const char* delim;
        c->unpack->mode = UNPACK_BY_DELIMITER;
        lua_getfield(L, 2, "delimiter");
        delim = luaL_optlstring(L, -1, "", &dlen);
        if (dlen == 0 || dlen > PACKAGE_MAX_DELIMITER_BYTES) {
            return luaL_error(L, "conn:setUnpack: delimiter must be 1..%u bytes", PACKAGE_MAX_DELIMITER_BYTES);
        }
        memcpy(c->unpack->delimiter, delim, dlen);
        c->unpack->delimiter_bytes = (unsigned short)dlen;
        lua_pop(L, 1);
    } else if (strcmp(mode, "length_field") == 0) {
        const char* coding;
        lua_Integer body_offset;
        lua_Integer field_offset;
        lua_Integer field_bytes;
        c->unpack->mode = UNPACK_BY_LENGTH_FIELD;
        lua_getfield(L, 2, "body_offset");
        body_offset = luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        lua_getfield(L, 2, "length_field_offset");
        field_offset = luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        lua_getfield(L, 2, "length_field_bytes");
        field_bytes = luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        if (body_offset <= 0 || body_offset > UINT16_MAX ||
            field_offset < 0 || field_offset > UINT16_MAX ||
            field_bytes <= 0 || field_bytes > 8 ||
            body_offset < field_offset + field_bytes ||
            (lua_Unsigned)body_offset > c->unpack->package_max_length) {
            return luaL_error(L, "conn:setUnpack: invalid length_field layout");
        }
        c->unpack->body_offset = (unsigned short)body_offset;
        c->unpack->length_field_offset = (unsigned short)field_offset;
        c->unpack->length_field_bytes = (unsigned short)field_bytes;
        lua_getfield(L, 2, "length_adjustment");
        value = luaL_optinteger(L, -1, 0);
        if (value < INT16_MIN || value > INT16_MAX) {
            return luaL_error(L, "conn:setUnpack: invalid length_adjustment");
        }
        c->unpack->length_adjustment = (short)value;
        lua_pop(L, 1);
        lua_getfield(L, 2, "length_field_coding");
        coding = luaL_optstring(L, -1, "be");
        if      (strcmp(coding, "le") == 0)     c->unpack->length_field_coding = ENCODE_BY_LITTLE_ENDIAN;
        else if (strcmp(coding, "varint") == 0) c->unpack->length_field_coding = ENCODE_BY_VARINT;
        else if (strcmp(coding, "asn1") == 0)   c->unpack->length_field_coding = ENCODE_BY_ASN1;
        else                                    c->unpack->length_field_coding = ENCODE_BY_BIG_ENDIAN;
        lua_pop(L, 1);
    } else {
        return luaL_error(L, "conn:setUnpack: unknown mode '%s'", mode);
    }

    // hio_t stores only the pointer; c->unpack (owned by the conn) outlives it.
    hio_set_unpack(c->io, c->unpack);
    return 0;
}

// conn:write(data) -> nbytes | nil, err  (non-blocking, no suspend)
static int l_conn_write(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    size_t len = 0;
    const char* data;
    if (c->closed || c->io == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "closed");
        return 2;
    }
    data = luaL_checklstring(L, 2, &len);
    lua_pushinteger(L, hio_write(c->io, data, len));
    return 1;
}

// conn:close()
static int l_conn_close(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    if (c->io && !c->closed) {
        hio_close(c->io);  // triggers on_conn_close (which clears c->io)
    }
    return 0;
}

// conn:fd()
static int l_conn_fd(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    lua_pushinteger(L, (c->io && !c->closed) ? hio_fd(c->io) : -1);
    return 1;
}

// conn:peeraddr() -> "ip:port"
static int l_conn_peeraddr(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    if (c->io && !c->closed) {
        char addr[SOCKADDR_STRLEN] = {0};
        SOCKADDR_STR(hio_peeraddr(c->io), addr);
        lua_pushstring(L, addr);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_conn_gc(lua_State* L) {
    LuaConn* c = (LuaConn*)luaL_checkudata(L, 1, CONN_META);
    if (c->io && !c->closed) {
        hevent_set_userdata(c->io, NULL);  // detach: no resume into a dead conn
        hio_close(c->io);
    }
    if (c->unpack) { HV_FREE(c->unpack); c->unpack = NULL; }
    return 0;
}

// ============================================================================
// TCP server: hv.tcpServer(host, port, on_conn) -> true | nil, err
//             (alias: hv.listen)
//
// on_conn(conn) runs in a fresh coroutine per accepted connection, so it can
// use conn:read()/write() synchronously. All on the current loop thread.
//
// The on_conn handler is stored in the registry keyed by the LISTEN io pointer.
// libhv copies the listen io's userdata to each accepted io (nio.c: connio->
// userdata = io->userdata), so the accept callback recovers the listen io ptr
// from the accepted io's userdata and uses it as the registry key.
// ============================================================================
static void on_server_accept(hio_t* io) {
    hloop_t* loop = hevent_loop(io);
    lua_State* L = (lua_State*)hloop_lua_state(loop);
    void* listen_key = hevent_userdata(io);   // inherited listen io ptr
    if (L == NULL || listen_key == NULL) return;

    lua_rawgetp(L, LUA_REGISTRYINDEX, listen_key);  // on_conn fn
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }

    // wrap accepted io in a conn (this overwrites io userdata -> conn ptr)
    conn_push_new(L, io);                            // stack: fn, conn
    // run on_conn(conn) in a fresh coroutine (moves fn+conn into it)
    hvlua_start_task(L, 1, NULL, NULL);
}

static int l_hv_tcpServer(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);
    hloop_t* loop = hvlua_loop(L);
    hio_t* listenio;

    luaL_checktype(L, 3, LUA_TFUNCTION);

    listenio = hloop_create_tcp_server(loop, host, port, on_server_accept);
    if (listenio == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "hv.tcpServer: create server failed");
        return 2;
    }

    // registry[listenio] = on_conn; accepted ios inherit userdata=listenio,
    // which the accept callback uses as this key.
    lua_pushvalue(L, 3);
    lua_rawsetp(L, LUA_REGISTRYINDEX, listenio);
    hevent_set_userdata(listenio, listenio);

    lua_pushboolean(L, 1);
    return 1;
}

// ============================================================================
// UDP: hv.udpClient(host, port) -> sock ; hv.udpServer(host, port, on_recv)
//
// UDP has no connection/accept. A udp "sock" reuses the conn userdata (it wraps
// an hio_t). sock:recvfrom() coroutine-suspends for one datagram and returns
// (data, peeraddr). sock:sendto(data) sends to the bound peer (client) or the
// last peer (server reply). All on the current loop thread.
// ============================================================================

// UDP read callback: push data + peeraddr string, resume with 2 results.
static void on_udp_read(hio_t* io, void* buf, int len) {
    LuaConn* c = (LuaConn*)hevent_userdata(io);
    lua_State* co;
    char addr[SOCKADDR_STRLEN];
    if (c == NULL) return;
    // Synchronous completion: hio_read found buffered data and called us inline,
    // before the coroutine yielded (same hazard as TCP on_conn_read). co is not
    // set yet; push onto the running coroutine and let l_udp_recvfrom return the
    // results directly instead of resuming.
    if (c->reading_L) {
        lua_pushlstring(c->reading_L, (const char*)buf, len);
        addr[0] = '\0';
        SOCKADDR_STR(hio_peeraddr(io), addr);
        lua_pushstring(c->reading_L, addr);
        c->read_done = 1;
        c->read_nres = 2;
        return;
    }
    if (c->co == NULL) return;
    co = hvlua_coroutine_state(c->co);
    if (co == NULL) { hvlua_cancel(c->co); c->co = NULL; return; }
    lua_pushlstring(co, (const char*)buf, len);
    addr[0] = '\0';
    SOCKADDR_STR(hio_peeraddr(io), addr);
    lua_pushstring(co, addr);
    conn_resume(c, 2);
}

static int recvfrom_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    return 2;  // data, peeraddr  (or nil,err on close)
}

// sock:recvfrom() -> data, peeraddr | nil, err
static int l_udp_recvfrom(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    // Arm the read callback and enter the "reading" window BEFORE hio_read:
    // hio_read may invoke on_udp_read inline (buffered datagram / read_remain),
    // and resuming the not-yet-yielded coroutine would be illegal. If it fires
    // synchronously, on_udp_read pushes (data, peer) and sets read_done, and
    // conn_end_read returns them directly instead of suspending. (Same fix as
    // the TCP read path.)
    hio_setcb_read(c->io, on_udp_read);
    c->reading_L = L;
    c->read_done = 0;
    c->read_nres = 0;
    hio_read(c->io);
    c->reading_L = NULL;
    if (c->read_done) {
        return c->read_nres;   // data, peer already pushed on L
    }
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    c->co = hvlua_suspend(L);
    return lua_yieldk(L, 0, (lua_KContext)0, recvfrom_k);
}

// sock:sendto(data) -> nbytes  (to bound peer / last recvfrom peer)
static int l_udp_sendto(lua_State* L) {
    LuaConn* c = lua_check_conn(L);
    size_t len = 0;
    const char* data;
    if (c->closed || c->io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    data = luaL_checklstring(L, 2, &len);
    lua_pushinteger(L, hio_write(c->io, data, len));
    return 1;
}

static int l_hv_udpClient(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);
    hloop_t* loop = hvlua_loop(L);
    hio_t* io = hloop_create_udp_client(loop, host, port);
    if (io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "hv.udpClient: create failed"); return 2;
    }
    conn_push_new(L, io);  // returns conn userdata on stack
    return 1;
}

// hv.udpServer(host, port, on_recv): on_recv(sock, data, peeraddr) per datagram.
static void on_udp_server_read(hio_t* io, void* buf, int len) {
    hloop_t* loop = hevent_loop(io);
    lua_State* L = (lua_State*)hloop_lua_state(loop);
    void* key = hevent_userdata(io);
    char addr[SOCKADDR_STRLEN];
    if (L == NULL || key == NULL) return;
    lua_rawgetp(L, LUA_REGISTRYINDEX, key);      // on_recv fn
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    // args: sock (the server conn userdata, stashed in registry too), data, peer
    lua_rawgetp(L, LUA_REGISTRYINDEX, (char*)key + 1);  // sock userdata
    lua_pushlstring(L, (const char*)buf, len);
    addr[0] = '\0';
    SOCKADDR_STR(hio_peeraddr(io), addr);
    lua_pushstring(L, addr);
    // run on_recv(sock, data, peer) in a fresh coroutine
    hvlua_start_task(L, 3, NULL, NULL);
}

static int l_hv_udpServer(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);
    hloop_t* loop = hvlua_loop(L);
    hio_t* io;
    LuaConn* c;

    luaL_checktype(L, 3, LUA_TFUNCTION);
    io = hloop_create_udp_server(loop, host, port);
    if (io == NULL) {
        lua_pushnil(L); lua_pushstring(L, "hv.udpServer: create failed"); return 2;
    }

    // registry[io] = on_recv ; registry[io+1] = sock userdata
    lua_pushvalue(L, 3);
    lua_rawsetp(L, LUA_REGISTRYINDEX, io);
    c = conn_push_new(L, io);       // pushes sock userdata; sets userdata=c
    (void)c;
    lua_rawsetp(L, LUA_REGISTRYINDEX, (char*)io + 1);  // pops the sock userdata
    // the read callback keys off hevent_userdata(io); set it back to io (not c)
    hevent_set_userdata(io, io);

    hio_setcb_read(io, on_udp_server_read);
    hio_read(io);

    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg conn_methods[] = {
    { "read",      l_conn_read      },
    { "readbytes", l_conn_readbytes },
    { "readuntil", l_conn_readuntil },
    { "readline",  l_conn_readline  },
    { "setUnpack", l_conn_setUnpack },
    { "write",     l_conn_write     },
    { "close",     l_conn_close     },
    { "fd",        l_conn_fd        },
    { "peeraddr",  l_conn_peeraddr  },
    { "sendto",    l_udp_sendto     },
    { "recvfrom",  l_udp_recvfrom   },
    { NULL, NULL }
};

static void register_conn(lua_State* L) {
    if (luaL_newmetatable(L, CONN_META)) {
        lua_pushcfunction(L, l_conn_gc);
        lua_setfield(L, -2, "__gc");
        lua_newtable(L);
        luaL_setfuncs(L, conn_methods, 0);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

static const luaL_Reg hloop_funcs[] = {
    { "setTimeout",  l_hloop_setTimeout  },
    { "setInterval", l_hloop_setInterval },
    { "clearTimer",  l_hloop_clearTimer  },
    { "sleep",       l_hloop_sleep       },
    { "resolveDns",  l_hloop_resolveDns  },
    // Primary names mirror the C++ classes hv::TcpClient / TcpServer /
    // UdpClient / UdpServer for a consistent 2x2 (client/server x tcp/udp).
    { "tcpClient",   l_hv_connect        },
    { "tcpServer",   l_hv_tcpServer      },
    { "udpClient",   l_hv_udpClient      },
    { "udpServer",   l_hv_udpServer      },
    // Aliases: connect/listen are idiomatic verbs kept for convenience.
    { "connect",     l_hv_connect        },
    { "listen",      l_hv_tcpServer      },
    { "run",         l_hloop_run         },
    { "stop",        l_hloop_stop        },
    { NULL, NULL }
};

// Register the event-loop primitives into the global "hv" table:
//   hv.setTimeout / hv.setInterval / hv.clearTimer / hv.sleep / hv.resolveDns /
//   hv.tcpClient (alias hv.connect) / hv.tcpServer (alias hv.listen) /
//   hv.udpClient / hv.udpServer / hv.run / hv.stop
// These operate on the current thread's event loop.
void hvlua_open_event(lua_State* L) {
    register_conn(L);
    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_setfuncs(L, hloop_funcs, 0);
    lua_setglobal(L, "hv");
}
