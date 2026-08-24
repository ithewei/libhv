#ifndef HV_JS_H_
#define HV_JS_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <quickjs.h>

#include "EventLoop.h"
#include "hexport.h"

namespace hv {
namespace js {

struct HvJsTask;
struct HvJsPromiseOp;

// Per-loop cleanup hook. Bindings that create loop-bound state (e.g. a per-loop
// AsyncHttpClient) register a (fn, userdata) pair; runtime teardown invokes each
// fn(userdata) before the JSRuntime is freed. Mirrors the lua binding's
// hvlua_cleanup_add. The void* keeps hvjs.h free of optional-module headers.
struct HvJsCleanup {
    void (*fn)(void* userdata);
    void* userdata;
};

struct HV_EXPORT HvJsRuntime {
    JSRuntime* rt;
    hloop_t* loop;
    HvJsTask* current_task;
    std::vector<HvJsTask*> tasks;
    std::vector<HvJsCleanup> cleanups;

    HvJsRuntime();
};

struct HV_EXPORT HvJsTaskScope {
    HvJsRuntime* runtime;
    HvJsTask* current;
    HvJsTask* previous;

    explicit HvJsTaskScope(HvJsTask* task);
    ~HvJsTaskScope();
};

struct HV_EXPORT HvJsTask {
    typedef void (*FinishCallback)(HvJsTask* task, JSValue result);

    HvJsRuntime* runtime;
    JSContext* js;
    JSValue promise;
    JSValue promise_result;
    bool promise_settled;
    bool promise_rejected;
    bool finished;
    bool drain_scheduled;
    int in_call;
    bool closing;
    int refcount;
    uint64_t start_hrtime;
    int timeout_ms;
    htimer_t* timeout_timer;
    std::string error;
    FinishCallback finish;
    std::vector<HvJsPromiseOp*> ops;
    std::vector<HvJsPromiseOp*> deferred_ops;

    HvJsTask();
    virtual ~HvJsTask();
};

struct HV_EXPORT HvJsPromiseOp {
    HvJsTask* task;
    JSValue resolve;
    JSValue reject;
    bool completed;
    bool defer_delete;
    std::shared_ptr<HvJsPromiseOp*> handle;

    HvJsPromiseOp();
    virtual ~HvJsPromiseOp();
    virtual void cancel(const char* reason);
};

HV_EXPORT HvJsRuntime* hvjs_runtime(hloop_t* loop);

// Run a script on `loop` using the per-loop QuickJS runtime and a fresh
// JSContext. The script body is wrapped in an async function, so top-level
// await is supported. Global `require`, `print`, and `arg` are installed.
// @return 1 if the script finished synchronously, 0 if it is pending on async
// work and the caller should run the loop, <0 on setup/load/runtime error.
// `exit_code`, if provided, is set to 1 when the script rejects or times out.
HV_EXPORT int hvjs_dofile(hloop_t* loop, const char* filepath, int argc = 0, char** argv = NULL, int* exit_code = NULL);
HV_EXPORT int hvjs_dostring(hloop_t* loop, const char* code, const char* filename = "<input>", int argc = 0, char** argv = NULL,
                            int* exit_code = NULL);

// Attach `task` to the loop's per-loop runtime: register it, create its
// JSContext, and (when timeout_ms > 0) arm a wall-clock timeout that aborts the
// task if the script runs too long. Returns false on failure (caller unrefs).
HV_EXPORT bool hvjs_runtime_add_task(HvJsRuntime* runtime, HvJsTask* task, int timeout_ms = 0);
// Register a per-loop cleanup hook, run on runtime teardown before the
// JSRuntime is freed. Used by bindings to own loop-bound state (e.g. a
// per-loop AsyncHttpClient) for the lifetime of the loop.
HV_EXPORT void hvjs_runtime_add_cleanup(HvJsRuntime* runtime, void (*fn)(void*), void* userdata);

// Raw per-loop hloop_t* the task runs on (stored on the runtime, NULL-safe).
HV_EXPORT hloop_t* hvjs_task_loop(HvJsTask* task);
HV_EXPORT void hvjs_task_ref(HvJsTask* task);
HV_EXPORT void hvjs_task_unref(HvJsTask* task);
HV_EXPORT void hvjs_task_add_op(HvJsTask* task, HvJsPromiseOp* op);
HV_EXPORT void hvjs_task_remove_op(HvJsTask* task, HvJsPromiseOp* op);

// Wrap `value` in Promise.resolve() as task->promise and attach the settle
// callback that records the outcome (promise_result / promise_settled /
// promise_rejected) once it settles. Takes ownership of `value` (frees it).
// Must be called inside a HvJsTaskScope. Returns false and sets *err on failure.
HV_EXPORT bool hvjs_task_await(HvJsTask* task, JSValue value, std::string* err = NULL);
// Run the pending job (microtask) queue once. Returns 1 if the task settled
// fulfilled, -1 if it settled rejected/errored, 0 if it is still pending on
// async work (the caller should run the loop). When it returns non-zero the
// task may already be freed — do not touch it afterwards.
HV_EXPORT int hvjs_task_poll(HvJsTask* task);
// Abort a task that has not settled yet: mark it closing, cancel pending ops and
// the timeout, and release the caller's reference. Used on setup/eval failures.
HV_EXPORT void hvjs_task_close(HvJsTask* task, const char* reason);

template <typename T> JSValue hvjs_new_promise(JSContext* js, HvJsTask* task, T** out) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(js, funcs);
    if (JS_IsException(promise)) return promise;
    if (task == NULL) {
        JS_FreeValue(js, funcs[0]);
        JS_FreeValue(js, funcs[1]);
        JS_FreeValue(js, promise);
        return JS_ThrowInternalError(js, "invalid hvjs task");
    }
    T* op = new T();
    op->task = task;
    op->resolve = funcs[0];
    op->reject = funcs[1];
    if (op->handle) {
        *op->handle = op;
    }
    hvjs_task_ref(task);
    hvjs_task_add_op(task, op);
    *out = op;
    return promise;
}

HV_EXPORT void hvjs_promise_resolve(HvJsPromiseOp* op, JSValue value);
HV_EXPORT void hvjs_promise_reject(HvJsPromiseOp* op, const char* message);
HV_EXPORT JSValue hvjs_rejected_promise(JSContext* js, const char* message);
HV_EXPORT JSValue hvjs_async_resolved_promise(JSContext* js, HvJsTask* task, JSValue value);
// Flush the task's deferred-delete ops once the binding call that may have
// completed an op synchronously (guarded by task->in_call) has returned. Safe
// no-op while another in_call frame is still open. Call after --task->in_call.
HV_EXPORT void hvjs_finish_deferred_ops(HvJsTask* task);

HV_EXPORT std::string hvjs_to_string(JSContext* ctx, JSValueConst value);
HV_EXPORT std::string hvjs_exception_string(JSContext* ctx);
HV_EXPORT bool hvjs_get_property(JSContext* js, JSValueConst obj, const char* name, JSValue* out);
HV_EXPORT std::string hvjs_get_string_property(JSContext* js, JSValueConst obj, const char* name, const char* defvalue = "");
HV_EXPORT int hvjs_get_int_property(JSContext* js, JSValueConst obj, const char* name, int defvalue = 0);
HV_EXPORT bool hvjs_get_bool_property(JSContext* js, JSValueConst obj, const char* name, bool defvalue = false);
HV_EXPORT HvJsTask* hvjs_get_task(JSContext* js);
HV_EXPORT void hvjs_new_class_id(JSClassID* class_id);

HV_EXPORT JSValue hvjs_require(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);

#ifdef HVJS_WITH_HTTP
HV_EXPORT JSValue hvjs_require_http(JSContext* js);
HV_EXPORT JSValue hvjs_require_ws(JSContext* js);
#endif
#ifdef HVJS_WITH_REDIS
HV_EXPORT JSValue hvjs_require_redis(JSContext* js);
#endif
#ifdef HVJS_WITH_MQTT
HV_EXPORT JSValue hvjs_require_mqtt(JSContext* js);
#endif

} // namespace js
} // namespace hv

#endif // HV_JS_H_
