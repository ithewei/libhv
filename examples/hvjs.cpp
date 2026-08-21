// hvjs: standalone QuickJS runtime on top of libhv's event loop.
//
// Usage: hvjs script.js [args...]
//
// The runtime publishes a shared EventLoop as this thread's loop so async JS
// bindings can reuse libhv clients on the same event loop. Scripts may use
// async/await with the built-in modules exposed through require("hv"),
// require("hv/http"), require("hv/ws"), require("hv/redis") and
// require("hv/mqtt") when the corresponding libhv modules are enabled.

#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>

#include <quickjs.h>

#include "EventLoop.h"
#include "hfile.h"
#include "hlog.h"
#include "htime.h"
#include "hvjs.h"

namespace {

struct HvJsCliTask : public hv::js::HvJsTask {
    int* exit_code;

    HvJsCliTask() : exit_code(NULL) {}
};

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s script.js [args...]\n", prog);
}

static bool load_file(const char* filepath, std::string* out) {
    HFile file;
    if (file.open(filepath, "rb") != 0) {
        return false;
    }
    size_t size = hv_filesize(filepath);
    out->resize(size);
    if (size == 0) return true;
    int nread = file.read(&(*out)[0], (int)size);
    return nread >= 0 && (size_t)nread == size;
}

static JSValue js_print(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) fputc(' ', stdout);
        std::string s = hv::js::hvjs_to_string(js, argv[i]);
        fputs(s.c_str(), stdout);
    }
    fputc('\n', stdout);
    return JS_UNDEFINED;
}

static void set_args(JSContext* js, int argc, char** argv) {
    JSValue arr = JS_NewArray(js);
    for (int i = 1; i < argc; ++i) {
        JS_SetPropertyUint32(js, arr, i - 1, JS_NewString(js, argv[i]));
    }
    JSValue global = JS_GetGlobalObject(js);
    JS_SetPropertyStr(js, global, "arg", arr);
    JS_FreeValue(js, global);
}

static void finish(hv::js::HvJsTask* base, JSValue result) {
    HvJsCliTask* task = static_cast<HvJsCliTask*>(base);
    if (task->finished) return;
    task->finished = true;
    if (!task->error.empty()) {
        fprintf(stderr, "hvjs: %s\n", task->error.c_str());
        if (task->exit_code) *task->exit_code = 1;
    }
    else if (task->promise_rejected) {
        std::string err = hv::js::hvjs_to_string(task->js, result);
        fprintf(stderr, "hvjs: %s\n", err.c_str());
        if (task->exit_code) *task->exit_code = 1;
    }
    JS_FreeValue(task->js, result);
    hv::js::hvjs_task_cancel_timeout(task);
    if (task->loop_ptr && task->loop_ptr->isRunning()) {
        task->loop_ptr->stop();
    }
    else if (task->loop && hloop_status(task->loop) == HLOOP_STATUS_RUNNING) {
        hloop_stop(task->loop);
    }
    hv::js::hvjs_task_unref(task);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* script = argv[1];
    std::string code;
    if (!load_file(script, &code)) {
        fprintf(stderr, "hvjs: failed to read %s\n", script);
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    hlog_set_handler(stdout_logger);

    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();
    hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, loop.get());

    HvJsCliTask* task = new HvJsCliTask();
    int exit_code = 0;
    task->exit_code = &exit_code;
    task->loop_ptr = loop;
    task->loop = loop->loop();
    task->finish = finish;
    hv::js::hvjs_task_set_runtime(task, hv::js::hvjs_runtime(task->loop));
    task->js = task->runtime ? JS_NewContext(task->runtime->rt) : NULL;
    if (task->runtime == NULL || task->js == NULL) {
        fprintf(stderr, "hvjs: failed to create quickjs runtime\n");
        hv::js::hvjs_task_unref(task);
        return 1;
    }
    JS_SetContextOpaque(task->js, task);
    task->timeout_ms = 30000;
    task->start_hrtime = gethrtime_us();
    if (!hv::js::hvjs_task_start_timeout(task, task->timeout_ms)) {
        fprintf(stderr, "hvjs: failed to create timeout timer\n");
        hv::js::hvjs_task_unref(task);
        return 1;
    }
    set_args(task->js, argc, argv);

    {
        hv::js::HvJsTaskScope scope(task);
        JSValue global = JS_GetGlobalObject(task->js);
        JS_SetPropertyStr(task->js, global, "require", JS_NewCFunction(task->js, hv::js::hvjs_require, "require", 1));
        JS_SetPropertyStr(task->js, global, "print", JS_NewCFunction(task->js, js_print, "print", 1));

        std::string wrapped = "(async function(){\n";
        wrapped += code;
        wrapped += "\n})()";
        JSValue eval = JS_Eval(task->js, wrapped.c_str(), wrapped.size(), script, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(eval)) {
            std::string err = hv::js::hvjs_exception_string(task->js);
            JS_FreeValue(task->js, global);
            fprintf(stderr, "hvjs: %s\n", err.c_str());
            hv::js::hvjs_task_cancel_timeout(task);
            hv::js::hvjs_task_unref(task);
            return 1;
        }

        JSValue promise_ctor = JS_GetPropertyStr(task->js, global, "Promise");
        JSValue promise_resolve = JS_GetPropertyStr(task->js, promise_ctor, "resolve");
        JS_FreeValue(task->js, global);
        JSValue promise_arg = eval;
        task->promise = JS_Call(task->js, promise_resolve, promise_ctor, 1, &promise_arg);
        JS_FreeValue(task->js, promise_resolve);
        JS_FreeValue(task->js, promise_ctor);
        JS_FreeValue(task->js, eval);
        if (JS_IsException(task->promise)) {
            std::string err = hv::js::hvjs_exception_string(task->js);
            fprintf(stderr, "hvjs: %s\n", err.c_str());
            task->closing = true;
            hv::js::hvjs_task_cancel_ops(task, "javascript handler error");
            hv::js::hvjs_task_cancel_timeout(task);
            hv::js::hvjs_task_unref(task);
            return 1;
        }
        std::string err;
        if (!hv::js::hvjs_watch_promise(task, &err)) {
            fprintf(stderr, "hvjs: %s\n", err.c_str());
            task->closing = true;
            hv::js::hvjs_task_cancel_ops(task, "javascript handler error");
            hv::js::hvjs_task_cancel_timeout(task);
            hv::js::hvjs_task_unref(task);
            return 1;
        }
    }

    hv::js::hvjs_task_ref(task);
    hv::js::hvjs_drain_jobs(task);
    bool finished = task->finished;
    hv::js::hvjs_task_unref(task);
    if (!finished) {
        loop->run();
    }
    hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, NULL);
    return exit_code;
}
