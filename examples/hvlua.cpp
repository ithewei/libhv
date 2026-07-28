// hvlua: standalone Lua runtime on top of libhv's event loop.
//
// Usage: hvlua script.lua [args...]
//
// Initializes the current thread's hloop lua_State, loads and runs the script
// (inside a coroutine so it may use synchronous-style async APIs), then runs
// the event loop so timers / async IO can complete.
#include <stdio.h>
#include <string.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hloop.h"
#include "hlog.h"
#include "hv_lua.h"

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s script.lua [args...]\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* script = argv[1];

    // Route hv.log() to stdout for a CLI runtime (default logger writes a file).
    hlog_set_handler(stdout_logger);

    // AUTO_FREE: the loop frees itself (and closes its lua_State via the dtor)
    //   when hloop_run returns; we must NOT call hloop_free afterwards.
    // QUIT_WHEN_NO_ACTIVE_EVENTS: exit once the script and all timers/async
    //   work are done, so a script without an explicit hloop.stop() still ends.
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE | HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);
    if (loop == NULL) {
        fprintf(stderr, "hvlua: failed to create event loop\n");
        return 1;
    }

    // Create the per-loop lua_State and expose script args as global `arg`.
    lua_State* L = hv::hvlua_state(loop);
    if (L == NULL) {
        fprintf(stderr, "hvlua: failed to create lua state\n");
        return 1;
    }
    lua_createtable(L, argc - 1, 0);
    for (int i = 1; i < argc; ++i) {
        lua_pushstring(L, argv[i]);
        lua_seti(L, -2, i - 1);   // arg[0] = script, arg[1..] = extra args
    }
    lua_setglobal(L, "arg");

    // Run the script (may yield on async ops).
    if (hv::hvlua_dofile(loop, script) != 0) {
        // hloop_run has not been entered; with AUTO_FREE we still must not call
        // hloop_free. Let process exit reclaim resources.
        return 1;
    }

    // Drive the loop until the script calls hloop.stop() or no work remains.
    // The loop auto-frees itself (and closes the lua_State) on return.
    hloop_run(loop);
    return 0;
}
