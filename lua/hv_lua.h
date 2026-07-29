#ifndef HV_LUA_H_
#define HV_LUA_H_

// libhv Lua binding: entry points.
//
// Design (see docs/superpowers/specs/2026-07-28-lua-binding-design.md):
//   - one lua_State per loop-thread, stored on hloop_t via hloop_set_lua_state.
//   - coroutine-based synchronous-style async IO: suspendable bindings call
//     hvlua_suspend()/lua_yieldk() and are resumed on the same loop thread when
//     the libhv async callback fires (hvlua_resume()).
//   - two layers of modules: hloop.* + core scheduler are plain C (no evpp/http
//     dependency); higher-level helpers like hv.json are C++.
//
// This header is usable from both C and C++. The core scheduler / hloop.* /
// hv.dns bindings are implemented in C; hv.json (nlohmann) is implemented in
// C++. C++ callers may use the hv:: aliases at the bottom.

#include "hloop.h"   // hloop_t + hexport.h (BEGIN_EXTERN_C) + hplatform (bool)

#ifndef lua_h
typedef struct lua_State lua_State;
#endif

// Opaque suspend token (see hvlua_suspend).
typedef struct HvLuaCoroutine HvLuaCoroutine;

// Task completion callback: ok = true on normal finish; on error ok = false and
// the error message is on top of `co`'s stack.
typedef void (*hvlua_done_cb)(void* ud, bool ok, lua_State* co);

BEGIN_EXTERN_C

// Get (creating on first use) the lua_State bound to `loop`. The state is
// stored on the hloop_t and closed when the loop is cleaned up. Registers all
// hloop.* / hv.* modules on creation. Returns NULL if `loop` is NULL.
lua_State* hvlua_state(hloop_t* loop);

// Run a script file on `loop`'s lua_State inside a fresh coroutine, so the
// script may use synchronous-style async APIs (which yield internally).
// @return 0 on success (or when the top coroutine yields), <0 on load/runtime error.
int hvlua_dofile(hloop_t* loop, const char* filepath);

// Run a script string on `loop`'s lua_State inside a fresh coroutine.
int hvlua_dostring(hloop_t* loop, const char* code);

// ---- coroutine scheduler (used by suspendable bindings) ----
//
// A suspendable binding does:
//   1. start the libhv async op, capturing a resume token for the running
//      coroutine via hvlua_suspend(L);
//   2. return lua_yieldk(L, ...) to suspend;
//   3. in the libhv async callback (same loop thread), push the results onto
//      the coroutine and call hvlua_resume(token, nresults).
//
// The token keeps the coroutine alive (luaL_ref in the registry) while it is
// suspended, and detects staleness so a late/duplicate resume is a safe no-op.

// Register the running coroutine `L` as suspendable; returns an opaque token.
// Must be called from a coroutine (a lua_State created by lua_newthread).
HvLuaCoroutine* hvlua_suspend(lua_State* L);

// Resume a previously suspended coroutine. `nresults` values must already be
// pushed on the coroutine. Safe no-op if the token is stale. Frees the token.
void hvlua_resume(HvLuaCoroutine* co, int nresults);

// Discard a suspend token WITHOUT resuming (e.g. an async op failed to start
// before the coroutine yielded). Releases the registry ref and frees the token.
void hvlua_cancel(HvLuaCoroutine* co);

// The coroutine's lua_State (to push results before hvlua_resume). NULL if stale.
lua_State* hvlua_coroutine_state(HvLuaCoroutine* co);

// Recover the owning hloop_t* for a lua_State (stashed at state creation).
hloop_t* hvlua_loop(lua_State* L);

// ---- coroutine "task" runner (used by the HTTP handler and hvlua) ----
//
// Runs fn(args...) inside a fresh coroutine. If the coroutine yields on an
// async op, it is resumed later (on the same loop thread) by hvlua_resume.
// When the coroutine finally finishes (success or error), `on_done` is invoked
// exactly once on the loop thread.
//
// The function and `nargs` arguments must already be pushed on `L` (a main or
// coroutine state); they are moved into the new coroutine. Returns 1 if the
// task finished synchronously (on_done already called), 0 if it yielded.
int hvlua_start_task(lua_State* L, int nargs, hvlua_done_cb on_done, void* ud);

// Module registration entry points (called by hvlua_state on state creation).
// Declared extern "C" so the C core can call the C++-implemented hv.json module.
void hvlua_open_hloop(lua_State* L);   // lua_hloop.c    -> global "hloop"
void hvlua_open_core(lua_State* L);    // lua_hv_core.cpp -> table "hv" (+ hv.json)
void hvlua_open_dns(lua_State* L);     // lua_hv_dns.c   -> hv.dns

END_EXTERN_C

#ifdef __cplusplus
// Convenience aliases so existing C++ call sites can use hv::hvlua_*.
namespace hv {
    using ::HvLuaCoroutine;
    using ::hvlua_done_cb;
    using ::hvlua_state;
    using ::hvlua_dofile;
    using ::hvlua_dostring;
    using ::hvlua_suspend;
    using ::hvlua_resume;
    using ::hvlua_cancel;
    using ::hvlua_coroutine_state;
    using ::hvlua_loop;
    using ::hvlua_start_task;
}
#endif

#endif // HV_LUA_H_
