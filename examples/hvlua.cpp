// hvlua: standalone Lua runtime on top of libhv's event loop.
//
// Usage: hvlua script.lua [args...]
//
// Binds an hv::EventLoop to this thread (so currentThreadEventLoop and the
// hv.* client bindings work exactly as they do inside an HTTP server IO
// thread), initializes the per-loop lua_State, loads and runs the script
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
#include "EventLoop.h"
#include "hvlua.h"

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
    // Line-buffer stdout so logs from long-running scripts (e.g. servers) appear
    // promptly even when stdout is redirected to a file/pipe (not a TTY).
    setvbuf(stdout, NULL, _IOLBF, 0);
    hlog_set_handler(stdout_logger);

    // QUIT_WHEN_NO_ACTIVE_EVENTS: exit once the script and all timers/async
    // work are done, so a script without an explicit hloop.stop() still ends.
    // (No AUTO_FREE: the EventLoop wrapper owns/frees this hloop.)
    hloop_t* hloop = hloop_new(HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);
    if (hloop == NULL) {
        fprintf(stderr, "hvlua: failed to create event loop\n");
        return 1;
    }

    // Wrap in an EventLoop and publish it as this thread's loop, so
    // currentThreadEventLoop (used by hv.http etc.) resolves during the script.
    hv::EventLoop loop(hloop);
    hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, &loop);

    // Create the per-loop lua_State and expose script args as global `arg`.
    lua_State* L = hv::hvlua_state(hloop);
    if (L == NULL) {
        fprintf(stderr, "hvlua: failed to create lua state\n");
        hloop_free(&hloop);
        return 1;
    }
    lua_createtable(L, argc - 1, 0);
    for (int i = 1; i < argc; ++i) {
        lua_pushstring(L, argv[i]);
        lua_seti(L, -2, i - 1);   // arg[0] = script, arg[1..] = extra args
    }
    lua_setglobal(L, "arg");

    // Run the script (may yield on async ops), then drive the loop until the
    // script calls hloop.stop() or no work remains.
    if (hv::hvlua_dofile(hloop, script) == 0) {
        loop.run();   // sets TLS again + hloop_run
    }
    hloop_free(&hloop);
    return 0;
}
