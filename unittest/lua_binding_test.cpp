/*
 * lua_binding_test — unit test for the libhv Lua binding core (lua/).
 *
 * Runs deterministically without network access. Drives the lua layer directly
 * (not the hvlua binary) and asserts observable side effects via a tiny
 * "probe" C function exposed to the scripts.
 *
 * Covered:
 *   1. hv.version / hv.json encode+decode roundtrip
 *   2. hv.setTimeout fires the callback
 *   3. hv.setInterval + clearTimer (including clearTimer from within the
 *      timer's own callback — the re-entrant free regression)
 *   4. hv.sleep suspends/resumes the coroutine (synchronous-style async)
 *   5. two coroutines interleave on one loop thread (concurrency, not parallel)
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hloop.h"
#include "hbase.h"
#include "hvlua.h"

// A probe table the scripts write to, so C can assert what happened.
static std::string g_probe;

static int l_probe(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (!g_probe.empty()) g_probe += ",";
    g_probe.append(s, len);
    return 0;
}

// Run a script string on a fresh AUTO_FREE loop, drive the loop to completion.
static void run_script(const char* code) {
    g_probe.clear();
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE | HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);
    assert(loop != NULL);

    lua_State* L = hv::hvlua_state(loop);
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");

    int ret = hv::hvlua_dostring(loop, code);
    assert(ret == 0);

    hloop_run(loop);   // AUTO_FREE closes the lua_State on return
}

static void test_core_json() {
    run_script(
        "local t = hv.json.decode('{\"a\":1,\"b\":[2,3],\"c\":\"x\"}')\n"
        "assert(t.a == 1)\n"
        "assert(t.b[1] == 2 and t.b[2] == 3)\n"
        "assert(t.c == 'x')\n"
        "assert(type(hv.version()) == 'string')\n"
        "local s = hv.json.encode({ok=true, n=42})\n"
        "local u = hv.json.decode(s)\n"
        "assert(u.ok == true and u.n == 42)\n"
        "probe('json-ok')\n"
    );
    assert(g_probe == "json-ok");
    printf("  test_core_json OK\n");
}

static void test_set_timeout() {
    run_script(
        "hv.setTimeout(10, function()\n"
        "  probe('fired')\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "fired");
    printf("  test_set_timeout OK\n");
}

static void test_interval_clear() {
    // clearTimer is called from within the timer's own callback (re-entrant).
    run_script(
        "local n = 0\n"
        "local id\n"
        "id = hv.setInterval(10, function()\n"
        "  n = n + 1\n"
        "  probe(tostring(n))\n"
        "  if n >= 3 then\n"
        "    hv.clearTimer(id)\n"
        "    hv.stop()\n"
        "  end\n"
        "end)\n"
    );
    assert(g_probe == "1,2,3");
    printf("  test_interval_clear OK\n");
}

static void test_sleep_coroutine() {
    // sleep suspends the coroutine; probe order proves it resumes after wait.
    run_script(
        "hv.setTimeout(1, function()\n"
        "  probe('a')\n"
        "  hv.sleep(30)\n"
        "  probe('b')\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "a,b");
    printf("  test_sleep_coroutine OK\n");
}

static void test_two_coroutines_interleave() {
    // A sleeps 20ms, B sleeps 50ms; both start together. Expected wake order:
    // A1,B1 (start), A2 (@~20), B2 stays..., so A's second probe precedes B's.
    run_script(
        "local done = 0\n"
        "local function worker(name, ms)\n"
        "  probe(name..'1')\n"
        "  hv.sleep(ms)\n"
        "  probe(name..'2')\n"
        "  done = done + 1\n"
        "  if done == 2 then hv.stop() end\n"
        "end\n"
        "hv.setTimeout(1, function() worker('A', 20) end)\n"
        "hv.setTimeout(1, function() worker('B', 60) end)\n"
    );
    // Both start (A1,B1 in some order), then A2 before B2 since A sleeps less.
    // Assert A2 appears before B2 and both first-probes precede both second.
    size_t a2 = g_probe.find("A2");
    size_t b2 = g_probe.find("B2");
    size_t a1 = g_probe.find("A1");
    size_t b1 = g_probe.find("B1");
    assert(a1 != std::string::npos && b1 != std::string::npos);
    assert(a2 != std::string::npos && b2 != std::string::npos);
    assert(a1 < a2 && b1 < b2);   // each worker's order preserved
    assert(a2 < b2);              // A (shorter sleep) wakes first
    printf("  test_two_coroutines_interleave OK\n");
}

// Regression: hv.json.encode on a cyclic table must not crash (depth cap +
// lua_checkstack); it returns a depth-truncated string, not a segfault.
static void test_json_cyclic() {
    run_script(
        "hv.setTimeout(1, function()\n"
        "  local t = {}; t.self = t\n"
        "  local s, e = hv.json.encode(t)\n"
        "  assert(type(s) == 'string' and #s > 0)\n"   // truncated, but produced
        "  probe('cyclic-ok')\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "cyclic-ok");
    printf("  test_json_cyclic OK\n");
}

// Regression: hv.json.encode on non-UTF-8 bytes must return (nil, err) instead
// of throwing an uncaught nlohmann exception that aborts the process.
static void test_json_non_utf8() {
    run_script(
        "hv.setTimeout(1, function()\n"
        "  local s, e = hv.json.encode({ x = '\\255\\254' })\n"
        "  assert(s == nil and type(e) == 'string')\n"
        "  probe('nonutf8-ok')\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "nonutf8-ok");
    printf("  test_json_non_utf8 OK\n");
}

static void test_json_decode_too_deep() {
    run_script(
        "local s = string.rep('[', 80)..'0'..string.rep(']', 80)\n"
        "local v, e = hv.json.decode(s)\n"
        "assert(v == nil and e == 'json too deep')\n"
        "probe('deep-json-ok')\n"
    );
    assert(g_probe == "deep-json-ok");
    printf("  test_json_decode_too_deep OK\n");
}

static void test_stop_before_run() {
    g_probe.clear();
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    assert(loop != NULL);
    lua_State* L = hv::hvlua_state(loop);
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");

    int ret = hv::hvlua_dostring(loop, "probe('before-stop'); hv.stop()");
    assert(ret == 0);
    assert(hloop_nactives(loop) > 0);
    hloop_run(loop);

    assert(g_probe == "before-stop");
    printf("  test_stop_before_run OK\n");
}

static void test_run_is_safe_noop() {
    run_script(
        "hv.run()\n"
        "probe('run-ok')\n"
    );
    assert(g_probe == "run-ok");
    printf("  test_run_is_safe_noop OK\n");
}

static long run_script_alloc_delta(const char* code) {
    long alloc = hv_alloc_cnt();
    long freed = hv_free_cnt();
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    assert(loop != NULL);
    assert(hv::hvlua_dostring(loop, code) == 0);
    hloop_run(loop);
    return (hv_alloc_cnt() - alloc) - (hv_free_cnt() - freed);
}

static void test_pending_async_cleanup() {
    long baseline = run_script_alloc_delta("hv.stop()");
    long pending = run_script_alloc_delta(
        "for i=1,100 do hv.setInterval(60000,function() end) end\n"
        "for i=1,100 do hv.setTimeout(1,function() hv.sleep(60000) end) end\n"
        "hv.setTimeout(30,function() hv.stop() end)\n"
    );
    assert(pending == baseline);
    printf("  test_pending_async_cleanup OK\n");
}

// Regression: a timer registered inside another timer's callback must survive
// after that callback's coroutine is GC'd (timer stores the per-loop main
// state, not the calling coroutine). Force GC between fires.
static void test_timer_registered_in_callback_gc() {
    run_script(
        "hv.setTimeout(20, function()\n"
        "  hv.setTimeout(60, function() probe('inner') end)\n"
        "end)\n"
        "hv.setInterval(10, function() collectgarbage('collect') end)\n"
        "hv.setTimeout(150, function() probe('done'); hv.stop() end)\n"
    );
    // Must reach both without crashing (pre-fix: UAF -> SIGSEGV, no 'inner').
    assert(g_probe.find("inner") != std::string::npos);
    assert(g_probe.find("done") != std::string::npos);
    printf("  test_timer_registered_in_callback_gc OK\n");
}

int main() {
    test_core_json();
    test_set_timeout();
    test_interval_clear();
    test_sleep_coroutine();
    test_two_coroutines_interleave();
    test_json_cyclic();
    test_json_non_utf8();
    test_stop_before_run();
    test_run_is_safe_noop();
    test_json_decode_too_deep();
    test_pending_async_cleanup();
    test_timer_registered_in_callback_gc();
    printf("ALL lua_binding_test PASSED\n");
    return 0;
}
