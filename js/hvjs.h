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

struct HV_EXPORT HvJsRuntimeOptions {
    size_t memory_limit;
    size_t stack_size;

    HvJsRuntimeOptions();
};

struct HV_EXPORT HvJsRuntime {
    JSRuntime* rt;
    HvJsRuntimeOptions options;
    HvJsTask* current_task;
    std::vector<HvJsTask*> tasks;

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
    hloop_t* loop;
    EventLoopPtr loop_ptr;
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
    TimerID timeout_timer_id;
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

HV_EXPORT HvJsRuntime* hvjs_runtime(hloop_t* loop, const HvJsRuntimeOptions& options);

HV_EXPORT void hvjs_task_set_runtime(HvJsTask* task, HvJsRuntime* runtime);
HV_EXPORT void hvjs_task_ref(HvJsTask* task);
HV_EXPORT void hvjs_task_unref(HvJsTask* task);
HV_EXPORT bool hvjs_task_start_timeout(HvJsTask* task, int timeout_ms);
HV_EXPORT void hvjs_task_cancel_timeout(HvJsTask* task);
HV_EXPORT void hvjs_task_add_op(HvJsTask* task, HvJsPromiseOp* op);
HV_EXPORT void hvjs_task_remove_op(HvJsTask* task, HvJsPromiseOp* op);
HV_EXPORT void hvjs_task_cancel_ops(HvJsTask* task, const char* reason);
HV_EXPORT void hvjs_schedule_drain(HvJsTask* task);
HV_EXPORT bool hvjs_watch_promise(HvJsTask* task, std::string* err = NULL);
HV_EXPORT void hvjs_drain_jobs(HvJsTask* task);

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
HV_EXPORT void hvjs_finish_deferred_op(HvJsPromiseOp* op);

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
