#ifdef WITH_JS

#include "hvjs.h"

#include <string.h>

#include <mutex>

#include "hlog.h"
#include "hversion.h"

namespace hv {
namespace js {

namespace {

struct HvJsSleep : public HvJsPromiseOp {
    htimer_t* timer;
    TimerID timer_id;

    HvJsSleep() : timer(NULL), timer_id(INVALID_TIMER_ID) {}
};

struct HvJsImmediatePromise : public HvJsPromiseOp {};

static JSClassID s_task_ref_class_id;
static std::once_flag s_task_ref_class_once;

std::mutex& js_class_id_mutex() {
    static std::mutex mutex;
    return mutex;
}

void drain_event_cb(hevent_t* ev) {
    HvJsTask* task = (HvJsTask*)hevent_userdata(ev);
    hvjs_drain_jobs(task);
    hvjs_task_unref(task);
}

void promise_complete(HvJsPromiseOp* op, JSValue value, bool ok) {
    HvJsTask* task = op->task;
    if (op->completed) {
        JS_FreeValue(task->js, value);
        return;
    }
    op->completed = true;
    if (!task->closing) {
        JSValue func = ok ? op->resolve : op->reject;
        JSValue ret = JS_Call(task->js, func, JS_UNDEFINED, 1, &value);
        if (JS_IsException(ret) && task->error.empty()) {
            task->error = hvjs_exception_string(task->js);
        }
        JS_FreeValue(task->js, ret);
        JS_FreeValue(task->js, value);
        JS_FreeValue(task->js, op->resolve);
        JS_FreeValue(task->js, op->reject);
        op->resolve = JS_UNDEFINED;
        op->reject = JS_UNDEFINED;
        if (task->in_call) {
            op->defer_delete = true;
            hvjs_schedule_drain(task);
            return;
        }
        hvjs_schedule_drain(task);
    }
    else {
        JS_FreeValue(task->js, value);
        JS_FreeValue(task->js, op->resolve);
        JS_FreeValue(task->js, op->reject);
        op->resolve = JS_UNDEFINED;
        op->reject = JS_UNDEFINED;
    }
    delete op;
    hvjs_task_unref(task);
}

void sleep_timer_cb(htimer_t* timer) {
    HvJsSleep* sleep = (HvJsSleep*)hevent_userdata(timer);
    hvjs_promise_resolve(sleep, JS_UNDEFINED);
}

void register_task_ref_class(JSContext* js) {
    std::call_once(s_task_ref_class_once, []() { hvjs_new_class_id(&s_task_ref_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_task_ref_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.js.task";
        JS_NewClass(rt, s_task_ref_class_id, &def);
    }
}

JSValue new_task_ref_value(JSContext* js, HvJsTask* task) {
    register_task_ref_class(js);
    JSValue obj = JS_NewObjectClass(js, s_task_ref_class_id);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, task);
    return obj;
}

JSValue promise_settle_cb(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    HvJsTask* task = (HvJsTask*)JS_GetOpaque(func_data[0], s_task_ref_class_id);
    if (task == NULL || task->closing || task->promise_settled) {
        return JS_UNDEFINED;
    }
    task->promise_result = JS_DupValue(js, argv[0]);
    task->promise_rejected = magic != 0;
    task->promise_settled = true;
    return JS_UNDEFINED;
}

JSValue js_hv_sleep(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    HvJsTask* task = hvjs_get_task(js);
    if (task == NULL || argc < 1) return JS_EXCEPTION;
    int32_t ms = 0;
    if (JS_ToInt32(js, &ms, argv[0]) != 0) return JS_EXCEPTION;
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(js, funcs);
    if (JS_IsException(promise)) return promise;

    HvJsSleep* sleep = new HvJsSleep();
    sleep->task = task;
    sleep->resolve = funcs[0];
    JS_FreeValue(js, funcs[1]);
    hvjs_task_ref(task);
    if (task->loop_ptr) {
        sleep->timer_id = task->loop_ptr->setTimeout(ms, [sleep](TimerID) { hvjs_promise_resolve(sleep, JS_UNDEFINED); });
    }
    else {
        sleep->timer = htimer_add(task->loop, sleep_timer_cb, (uint32_t)ms, 1);
        if (sleep->timer) hevent_set_userdata(sleep->timer, sleep);
    }
    if (sleep->timer == NULL && sleep->timer_id == INVALID_TIMER_ID) {
        hvjs_task_unref(task);
        JS_FreeValue(js, sleep->resolve);
        delete sleep;
        JS_FreeValue(js, promise);
        return JS_ThrowInternalError(js, "hv.sleep: failed to create timer");
    }
    return promise;
}

JSValue js_hv_version(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(js, HV_VERSION_STRING);
}

JSValue js_hv_log(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) line += "\t";
        line += hvjs_to_string(js, argv[i]);
    }
    hlogi("%s", line.c_str());
    return JS_UNDEFINED;
}

JSValue require_hv(JSContext* js) {
    JSValue hv = JS_NewObject(js);
    JS_SetPropertyStr(js, hv, "version", JS_NewCFunction(js, js_hv_version, "version", 0));
    JS_SetPropertyStr(js, hv, "log", JS_NewCFunction(js, js_hv_log, "log", 1));
    JS_SetPropertyStr(js, hv, "sleep", JS_NewCFunction(js, js_hv_sleep, "sleep", 1));
    return hv;
}

} // namespace

HvJsTask::HvJsTask()
    : rt(NULL), js(NULL), loop(NULL), promise(JS_UNDEFINED), promise_result(JS_UNDEFINED), promise_settled(false), promise_rejected(false), finished(false),
      in_call(false), closing(false), refcount(1), finish(NULL) {}

HvJsTask::~HvJsTask() {}

HvJsPromiseOp::HvJsPromiseOp() : task(NULL), resolve(JS_UNDEFINED), reject(JS_UNDEFINED), completed(false), defer_delete(false) {}

HvJsPromiseOp::~HvJsPromiseOp() {}

void hvjs_task_ref(HvJsTask* task) {
    ++task->refcount;
}

void hvjs_task_unref(HvJsTask* task) {
    if (--task->refcount != 0) return;
    task->closing = true;
    if (!JS_IsUndefined(task->promise_result)) {
        JS_FreeValue(task->js, task->promise_result);
        task->promise_result = JS_UNDEFINED;
    }
    if (!JS_IsUndefined(task->promise)) {
        JS_FreeValue(task->js, task->promise);
        task->promise = JS_UNDEFINED;
    }
    if (task->js) {
        if (task->rt) {
            JS_RunGC(task->rt);
        }
        JS_FreeContext(task->js);
        task->js = NULL;
    }
    if (task->rt) {
        JS_RunGC(task->rt);
        JS_FreeRuntime(task->rt);
        task->rt = NULL;
    }
    delete task;
}

void hvjs_schedule_drain(HvJsTask* task) {
    if (task == NULL || task->closing) return;
    hvjs_task_ref(task);
    if (task->loop_ptr) {
        task->loop_ptr->queueInLoop([task]() {
            hvjs_drain_jobs(task);
            hvjs_task_unref(task);
        });
    }
    else if (task->loop) {
        hevent_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.cb = drain_event_cb;
        ev.userdata = task;
        hloop_post_event(task->loop, &ev);
    }
    else {
        hvjs_task_unref(task);
    }
}

bool hvjs_watch_promise(HvJsTask* task, std::string* err) {
    if (task == NULL || task->js == NULL || JS_IsUndefined(task->promise)) return false;
    JSContext* js = task->js;
    JSValue then = JS_GetPropertyStr(js, task->promise, "then");
    if (JS_IsException(then)) {
        if (err) *err = hvjs_exception_string(js);
        return false;
    }
    if (!JS_IsFunction(js, then)) {
        JS_FreeValue(js, then);
        if (err) *err = "javascript result is not thenable";
        return false;
    }

    JSValue task_ref = new_task_ref_value(js, task);
    if (JS_IsException(task_ref)) {
        JS_FreeValue(js, then);
        if (err) *err = hvjs_exception_string(js);
        return false;
    }
    JSValue on_fulfilled = JS_NewCFunctionData(js, promise_settle_cb, 1, 0, 1, &task_ref);
    JSValue on_rejected = JS_NewCFunctionData(js, promise_settle_cb, 1, 1, 1, &task_ref);
    if (JS_IsException(on_fulfilled) || JS_IsException(on_rejected)) {
        if (err) *err = hvjs_exception_string(js);
        JS_FreeValue(js, on_fulfilled);
        JS_FreeValue(js, on_rejected);
        JS_FreeValue(js, task_ref);
        JS_FreeValue(js, then);
        return false;
    }
    JSValue args[2] = {on_fulfilled, on_rejected};
    JSValue ret = JS_Call(js, then, task->promise, 2, args);
    JS_FreeValue(js, on_fulfilled);
    JS_FreeValue(js, on_rejected);
    JS_FreeValue(js, task_ref);
    JS_FreeValue(js, then);
    if (JS_IsException(ret)) {
        if (err) *err = hvjs_exception_string(js);
        JS_FreeValue(js, ret);
        return false;
    }
    JS_FreeValue(js, ret);
    return true;
}

void hvjs_drain_jobs(HvJsTask* task) {
    JSContext* job_ctx = NULL;
    while (JS_IsJobPending(task->rt)) {
        int rc = JS_ExecutePendingJob(task->rt, &job_ctx);
        if (rc < 0) {
            task->error = hvjs_exception_string(job_ctx ? job_ctx : task->js);
            break;
        }
    }
    if (!task->finished && task->promise_settled) {
        JSValue value = task->promise_result;
        task->promise_result = JS_UNDEFINED;
        if (task->finish) {
            task->finish(task, value);
        }
        else {
            JS_FreeValue(task->js, value);
            task->finished = true;
            hvjs_task_unref(task);
        }
        return;
    }
    if (!task->error.empty()) {
        if (task->finish) {
            task->finish(task, JS_UNDEFINED);
        }
        else {
            task->finished = true;
            hvjs_task_unref(task);
        }
    }
}

void hvjs_promise_resolve(HvJsPromiseOp* op, JSValue value) {
    promise_complete(op, value, true);
}

void hvjs_promise_reject(HvJsPromiseOp* op, const char* message) {
    promise_complete(op, JS_NewString(op->task->js, message ? message : "error"), false);
}

JSValue hvjs_rejected_promise(JSContext* js, const char* message) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(js, funcs);
    if (JS_IsException(promise)) return promise;
    JSValue reason = JS_NewString(js, message ? message : "error");
    JSValue ret = JS_Call(js, funcs[1], JS_UNDEFINED, 1, &reason);
    JS_FreeValue(js, ret);
    JS_FreeValue(js, reason);
    JS_FreeValue(js, funcs[0]);
    JS_FreeValue(js, funcs[1]);
    return promise;
}

JSValue hvjs_async_resolved_promise(JSContext* js, HvJsTask* task, JSValue value) {
    if (task == NULL) {
        JS_FreeValue(js, value);
        return JS_ThrowInternalError(js, "invalid hvjs task");
    }
    HvJsImmediatePromise* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsImmediatePromise>(js, task, &op);
    if (JS_IsException(promise)) {
        JS_FreeValue(js, value);
        return promise;
    }
    hvjs_promise_resolve(op, value);
    return promise;
}

void hvjs_finish_deferred_op(HvJsPromiseOp* op) {
    if (op == NULL || !op->completed || !op->defer_delete) return;
    HvJsTask* task = op->task;
    delete op;
    hvjs_task_unref(task);
}

std::string hvjs_to_string(JSContext* ctx, JSValueConst value) {
    size_t len = 0;
    const char* str = JS_ToCStringLen(ctx, &len, value);
    if (str == NULL) return std::string();
    std::string out(str, len);
    JS_FreeCString(ctx, str);
    return out;
}

std::string hvjs_exception_string(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    std::string msg = hvjs_to_string(ctx, exception);
    JS_FreeValue(ctx, exception);
    return msg.empty() ? "javascript exception" : msg;
}

bool hvjs_get_property(JSContext* js, JSValueConst obj, const char* name, JSValue* out) {
    *out = JS_UNDEFINED;
    if (!JS_IsObject(obj)) return false;
    *out = JS_GetPropertyStr(js, obj, name);
    return !JS_IsUndefined(*out) && !JS_IsException(*out);
}

std::string hvjs_get_string_property(JSContext* js, JSValueConst obj, const char* name, const char* defvalue) {
    JSValue value;
    if (!hvjs_get_property(js, obj, name, &value) || JS_IsNull(value)) {
        if (!JS_IsUndefined(value) && !JS_IsException(value)) JS_FreeValue(js, value);
        return defvalue;
    }
    std::string out = hvjs_to_string(js, value);
    JS_FreeValue(js, value);
    return out;
}

int hvjs_get_int_property(JSContext* js, JSValueConst obj, const char* name, int defvalue) {
    JSValue value;
    if (!hvjs_get_property(js, obj, name, &value) || JS_IsNull(value)) {
        if (!JS_IsUndefined(value) && !JS_IsException(value)) JS_FreeValue(js, value);
        return defvalue;
    }
    int32_t out = defvalue;
    JS_ToInt32(js, &out, value);
    JS_FreeValue(js, value);
    return out;
}

bool hvjs_get_bool_property(JSContext* js, JSValueConst obj, const char* name, bool defvalue) {
    JSValue value;
    if (!hvjs_get_property(js, obj, name, &value) || JS_IsNull(value)) {
        if (!JS_IsUndefined(value) && !JS_IsException(value)) JS_FreeValue(js, value);
        return defvalue;
    }
    bool out = JS_ToBool(js, value) != 0;
    JS_FreeValue(js, value);
    return out;
}

HvJsTask* hvjs_get_task(JSContext* js) {
    return (HvJsTask*)JS_GetContextOpaque(js);
}

void hvjs_new_class_id(JSClassID* class_id) {
    std::lock_guard<std::mutex> lock(js_class_id_mutex());
    JS_NewClassID(class_id);
}

JSValue hvjs_require(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(js, "require needs a module name");
    }
    std::string name = hvjs_to_string(js, argv[0]);
    if (name == "hv") {
        return require_hv(js);
    }
#ifdef HVJS_WITH_HTTP
    if (name == "hv/http") {
        return hvjs_require_http(js);
    }
#endif
#ifdef HVJS_WITH_REDIS
    if (name == "hv/redis") {
        return hvjs_require_redis(js);
    }
#endif
#ifdef HVJS_WITH_HTTP
    if (name == "hv/ws") {
        return hvjs_require_ws(js);
    }
#endif
#ifdef HVJS_WITH_MQTT
    if (name == "hv/mqtt") {
        return hvjs_require_mqtt(js);
    }
#endif
    return JS_ThrowReferenceError(js, "module '%s' is not available", name.c_str());
}

} // namespace js
} // namespace hv

#endif // WITH_JS
