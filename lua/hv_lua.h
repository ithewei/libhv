#ifndef HV_LUA_H_
#define HV_LUA_H_

// libhv Lua binding: public C++ entry points.
//
// Design (see docs/superpowers/specs/2026-07-28-lua-binding-design.md):
//   - one lua_State per loop-thread, stored on hloop_t via hloop_set_lua_state.
//   - coroutine-based synchronous-style async IO: suspendable C bindings call
//     hvlua_yield()/lua_yieldk() and are resumed on the same loop thread when
//     the libhv async callback fires (hvlua_resume()).
//   - two layers of modules: hloop.* (pure C hloop, no evpp/http dependency)
//     and hv.* (higher-level helpers/clients).

#include "hloop.h"

struct lua_State;

namespace hv {

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
//   2. return hvlua_yield(L, nresults_ignored, k) to suspend;
//   3. in the libhv async callback (same loop thread), push the results onto
//      the coroutine and call hvlua_resume(token, nresults).
//
// The token keeps the coroutine alive (luaL_ref in the registry) while it is
// suspended, and detects staleness so a late/duplicate resume is a safe no-op.
struct HvLuaCoroutine;

// Register the running coroutine `L` as suspendable; returns an opaque token.
// Must be called from a coroutine (a lua_State created by lua_newthread).
HvLuaCoroutine* hvlua_suspend(lua_State* L);

// Resume a previously suspended coroutine. `nresults` values must already be
// pushed on `co->L`. Safe no-op if the token is stale. Frees the token.
void hvlua_resume(HvLuaCoroutine* co, int nresults);

// Discard a suspend token WITHOUT resuming (e.g. an async op failed to start
// before the coroutine yielded). Releases the registry ref and frees the token.
void hvlua_cancel(HvLuaCoroutine* co);

// The coroutine's lua_State (to push results before hvlua_resume). NULL if stale.
lua_State* hvlua_coroutine_state(HvLuaCoroutine* co);

// Recover the owning hloop_t* for a lua_State (stashed at state creation).
hloop_t* hvlua_loop(lua_State* L);

} // namespace hv

#endif // HV_LUA_H_
