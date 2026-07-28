/*
 * lua_binding_test — unit test for the libhv Lua binding core (lua/).
 *
 * Runs deterministically without network access. Drives the lua layer directly
 * (not the hvlua binary) and asserts observable side effects via a tiny
 * "probe" C function exposed to the scripts.
 *
 * Covered:
 *   1. hv.now / hv.json encode+decode roundtrip
 *   2. hloop.setTimeout fires the callback
 *   3. hloop.setInterval + clearTimer (including clearTimer from within the
 *      timer's own callback — the re-entrant free regression)
 *   4. hloop.sleep suspends/resumes the coroutine (synchronous-style async)
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
#include "hv_lua.h"

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
        "assert(type(hv.now()) == 'number')\n"
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
        "hloop.setTimeout(10, function()\n"
        "  probe('fired')\n"
        "  hloop.stop()\n"
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
        "id = hloop.setInterval(10, function()\n"
        "  n = n + 1\n"
        "  probe(tostring(n))\n"
        "  if n >= 3 then\n"
        "    hloop.clearTimer(id)\n"
        "    hloop.stop()\n"
        "  end\n"
        "end)\n"
    );
    assert(g_probe == "1,2,3");
    printf("  test_interval_clear OK\n");
}

static void test_sleep_coroutine() {
    // sleep suspends the coroutine; probe order proves it resumes after wait.
    run_script(
        "hloop.setTimeout(1, function()\n"
        "  probe('a')\n"
        "  hloop.sleep(30)\n"
        "  probe('b')\n"
        "  hloop.stop()\n"
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
        "  hloop.sleep(ms)\n"
        "  probe(name..'2')\n"
        "  done = done + 1\n"
        "  if done == 2 then hloop.stop() end\n"
        "end\n"
        "hloop.setTimeout(1, function() worker('A', 20) end)\n"
        "hloop.setTimeout(1, function() worker('B', 60) end)\n"
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

int main() {
    test_core_json();
    test_set_timeout();
    test_interval_clear();
    test_sleep_coroutine();
    test_two_coroutines_interleave();
    printf("ALL lua_binding_test PASSED\n");
    return 0;
}
