// hvlua: standalone Lua runtime on top of libhv's event loop.
//
// Usage: hvlua script.lua [args...]
//
// Holds the loop as a shared_ptr<EventLoop> (make_shared) and publishes it as
// this thread's loop via EventLoop::run(). Using a shared_ptr (not a stack
// object) is required so the hv.* client bindings can obtain an EventLoopPtr
// for the current thread via currentThreadEventLoopPtr (EventLoop::
// shared_from_this()) and share this one loop/thread with AsyncHttpClient /
// AsyncRedisClient etc. The script runs inside a coroutine so it may use
// synchronous-style async APIs.
#include <stdio.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

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

    // A default-constructed EventLoop owns its own hloop_t (auto-freed on run
    // exit). Held via shared_ptr so currentThreadEventLoopPtr / shared_from_this
    // work — that is how the hv.* client bindings share this one loop/thread.
    // EventLoop::run() publishes it as this thread's loop (TLS) and returns when
    // the script calls hv.stop(). We do NOT quit on "no active events": async
    // work posted from a coroutine (e.g. hv.http.get) may not have registered
    // its socket/timer yet when that check runs; scripts terminate via hv.stop().
    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();

    // Create the per-loop lua_State and expose script args as global `arg`.
    lua_State* L = hv::hvlua_state(loop->loop());
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

    // Publish this thread's loop (TLS) before running the script, so bindings
    // that resolve currentThreadEventLoopPtr during script load also work.
    hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, loop.get());

    // Run the script (may yield on async ops), then drive the loop until the
    // script calls hv.stop() or no work remains. The lua_State is closed when
    // the owned hloop is torn down (dtor registered via hloop_set_lua_state).
    if (hv::hvlua_dofile(loop->loop(), script) == 0) {
        loop->run();   // re-sets TLS + hloop_run; frees the owned hloop on exit
    }
    return 0;
}
