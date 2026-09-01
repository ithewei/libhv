// hvjs: standalone QuickJS runtime on top of libhv's event loop.
//
// Usage: hvjs script.js [args...]
//
// Holds the loop as a shared_ptr<EventLoop> (make_shared) and publishes it as
// this thread's loop via EventLoop::run(). Using a shared_ptr (not a stack
// object) is required so the hv.* client bindings can obtain an EventLoopPtr
// for the current thread via currentThreadEventLoopPtr (EventLoop::
// shared_from_this()) and share this one loop/thread with AsyncHttpClient /
// AsyncRedisClient etc. The script body is wrapped in an async function, so it
// may use top-level await with the built-in modules exposed through
// require("hv"), require("hv/http"), require("hv/ws"), require("hv/redis") and
// require("hv/mqtt") when the corresponding libhv modules are enabled.
#include <stdio.h>

#include <memory>

#include "EventLoop.h"
#include "hlog.h"
#include "hvjs.h"

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s script.js [args...]\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* script = argv[1];

    // Route logs to stdout for a CLI runtime (default logger writes a file).
    // Line-buffer stdout so logs from long-running scripts appear promptly even
    // when stdout is redirected to a file/pipe (not a TTY).
    setvbuf(stdout, NULL, _IOLBF, 0);
    hlog_set_handler(stdout_logger);

    // A default-constructed EventLoop owns its own hloop_t (auto-freed on run
    // exit). Held via shared_ptr so currentThreadEventLoopPtr / shared_from_this
    // work — that is how the hv.* client bindings share this one loop/thread.
    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();

    // Publish this thread's loop (TLS) before running the script, so the js
    // task and bindings can resolve currentThreadEventLoopPtr during load.
    hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, loop.get());

    // Run the script (may await async ops). hvjs_dofile returns 0 when the
    // script is pending on async work and the caller should run the loop, 1 when
    // it finished synchronously, and <0 on setup/load/runtime error. The wrapped
    // async function settling stops the loop, so we do NOT need hv.stop() here.
    int exit_code = 0;
    int rc = hv::js::hvjs_dofile(loop->loop(), script, argc, argv, &exit_code);
    if (rc == 0) {
        loop->run();
    }
    return rc < 0 ? 1 : exit_code;
}
