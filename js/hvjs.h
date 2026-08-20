#ifndef HV_JS_H_
#define HV_JS_H_

#include <string>

#include <quickjs.h>

#include "EventLoop.h"
#include "hexport.h"

namespace hv {
namespace js {

struct HV_EXPORT HvJsTask {
    typedef void (*FinishCallback)(HvJsTask* task, JSValue result);

    JSRuntime* rt;
    JSContext* js;
    hloop_t* loop;
    EventLoopPtr loop_ptr;
    JSValue promise;
    bool finished;
    bool in_call;
    bool closing;
    int refcount;
    std::string error;
    FinishCallback finish;

    HvJsTask();
    virtual ~HvJsTask();
};

struct HV_EXPORT HvJsPromiseOp {
    HvJsTask* task;
    JSValue resolve;
    JSValue reject;
    bool completed;
    bool defer_delete;

    HvJsPromiseOp();
    virtual ~HvJsPromiseOp();
};

HV_EXPORT void hvjs_task_ref(HvJsTask* task);
HV_EXPORT void hvjs_task_unref(HvJsTask* task);
HV_EXPORT void hvjs_schedule_drain(HvJsTask* task);
HV_EXPORT void hvjs_drain_jobs(HvJsTask* task);

template <typename T> JSValue hvjs_new_promise(JSContext* js, HvJsTask* task, T** out) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(js, funcs);
    if (JS_IsException(promise)) return promise;
    T* op = new T();
    op->task = task;
    op->resolve = funcs[0];
    op->reject = funcs[1];
    hvjs_task_ref(task);
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
