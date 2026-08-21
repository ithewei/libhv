#ifdef WITH_JS

#include "hvjs.h"

#include <algorithm>
#include <string.h>

#include <stdio.h>
#include <mutex>

#include "hfile.h"
#include "hlog.h"
#include "htime.h"
#include "hversion.h"

namespace hv {
namespace js {

namespace {

struct HvJsSleep : public HvJsPromiseOp {
    htimer_t* timer;
    TimerID timer_id;

    HvJsSleep() : timer(NULL), timer_id(INVALID_TIMER_ID) {}
    void cancel(const char* reason) override;
};

struct HvJsImmediatePromise : public HvJsPromiseOp {};

struct HvJsScriptTask : public HvJsTask {
    int* exit_code;

    HvJsScriptTask() : exit_code(NULL) {}
};

static JSClassID s_task_ref_class_id;
static std::once_flag s_task_ref_class_once;

const size_t DEFAULT_JS_MEMORY_LIMIT = 64 * 1024 * 1024;
const size_t DEFAULT_JS_STACK_SIZE = 1024 * 1024;
const int DEFAULT_JS_TASK_TIMEOUT = 30000;

void delete_op(HvJsPromiseOp* op);

void runtime_dtor(void* userdata) {
    HvJsRuntime* runtime = (HvJsRuntime*)userdata;
    if (runtime == NULL) return;
    std::vector<HvJsTask*> tasks;
    tasks.swap(runtime->tasks);
    for (size_t i = 0; i < tasks.size(); ++i) {
        HvJsTask* task = tasks[i];
        if (task == NULL) continue;
        hvjs_task_ref(task);
        bool release_request_ref = !task->finished;
        task->error = "javascript runtime closed";
        task->closing = true;
        task->finished = true;
        task->in_call = 0;
        hvjs_task_cancel_timeout(task);
        hvjs_task_cancel_ops(task, task->error.c_str());
        if (task->drain_scheduled) {
            task->drain_scheduled = false;
            hvjs_task_unref(task);
        }
        if (release_request_ref) {
            hvjs_task_unref(task);
        }
        hvjs_task_unref(task);
    }
    if (runtime->rt) {
        JS_RunGC(runtime->rt);
        JS_FreeRuntime(runtime->rt);
        runtime->rt = NULL;
    }
    delete runtime;
}

int interrupt_handler(JSRuntime* rt, void* opaque) {
    (void)opaque;
    HvJsRuntime* runtime = (HvJsRuntime*)JS_GetRuntimeOpaque(rt);
    HvJsTask* task = runtime ? runtime->current_task : NULL;
    if (task == NULL || task->timeout_ms <= 0 || task->start_hrtime == 0) {
        return 0;
    }
    uint64_t elapsed_us = gethrtime_us() - task->start_hrtime;
    return elapsed_us >= (uint64_t)task->timeout_ms * 1000;
}

std::mutex& js_class_id_mutex() {
    static std::mutex mutex;
    return mutex;
}

void drain_event_cb(hevent_t* ev) {
    HvJsTask* task = (HvJsTask*)hevent_userdata(ev);
    if (task) {
        task->drain_scheduled = false;
    }
    hvjs_drain_jobs(task);
    hvjs_task_unref(task);
}

void finish_deferred_ops(HvJsTask* task) {
    if (task == NULL || task->in_call > 0 || task->deferred_ops.empty()) return;
    std::vector<HvJsPromiseOp*> ops;
    ops.swap(task->deferred_ops);
    for (size_t i = 0; i < ops.size(); ++i) {
        HvJsPromiseOp* op = ops[i];
        if (op == NULL || !op->completed || !op->defer_delete) continue;
        delete_op(op);
    }
}

void finish_ready_task(HvJsTask* task) {
    if (task == NULL) return;
    finish_deferred_ops(task);
    if (!task->finished && task->promise_settled) {
        JSValue value = task->promise_result;
        task->promise_result = JS_UNDEFINED;
        if (task->finish) {
            HvJsTaskScope scope(task);
            task->finish(task, value);
        }
        else {
            JS_FreeValue(task->js, value);
            task->finished = true;
            hvjs_task_unref(task);
        }
    }
    else if (!task->finished && !task->error.empty()) {
        if (task->finish) {
            HvJsTaskScope scope(task);
            task->finish(task, JS_UNDEFINED);
        }
        else {
            task->finished = true;
            hvjs_task_unref(task);
        }
    }
}

void delete_op(HvJsPromiseOp* op) {
    if (op == NULL) return;
    HvJsTask* task = op->task;
    if (op->handle) {
        *op->handle = NULL;
    }
    delete op;
    hvjs_task_unref(task);
}

void cancel_op(HvJsPromiseOp* op, const char* reason) {
    if (op == NULL) return;
    HvJsTask* task = op->task;
    JSContext* js = task ? task->js : NULL;
    op->completed = true;
    if (op->handle) {
        *op->handle = NULL;
    }
    op->cancel(reason);
    if (js) {
        JS_FreeValue(js, op->resolve);
        JS_FreeValue(js, op->reject);
    }
    op->resolve = JS_UNDEFINED;
    op->reject = JS_UNDEFINED;
    delete op;
    hvjs_task_unref(task);
}

void promise_complete(HvJsPromiseOp* op, JSValue value, bool ok) {
    if (op == NULL) return;
    HvJsTask* task = op->task;
    if (task == NULL || task->js == NULL) {
        return;
    }
    if (op->completed) {
        JS_FreeValue(task->js, value);
        return;
    }
    op->completed = true;
    hvjs_task_remove_op(task, op);
    if (!task->closing) {
        HvJsTaskScope scope(task);
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
        if (task->in_call > 0) {
            op->defer_delete = true;
            task->deferred_ops.push_back(op);
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
    delete_op(op);
}

void task_timeout_timer_cb(htimer_t* timer) {
    HvJsTask* task = (HvJsTask*)hevent_userdata(timer);
    if (task == NULL) return;
    hevent_set_userdata(timer, NULL);
    task->timeout_timer = NULL;
    if (!task->finished && !task->closing) {
        task->error = "javascript request timeout";
        task->closing = true;
        hvjs_task_cancel_ops(task, task->error.c_str());
        if (task->finish) {
            task->finish(task, JS_UNDEFINED);
        }
    }
    hvjs_task_unref(task);
}

void sleep_timer_cb(htimer_t* timer) {
    HvJsSleep* sleep = (HvJsSleep*)hevent_userdata(timer);
    if (sleep == NULL) return;
    sleep->timer = NULL;
    hvjs_promise_resolve(sleep, JS_UNDEFINED);
}

void HvJsSleep::cancel(const char* reason) {
    if (timer_id != INVALID_TIMER_ID && task && task->loop_ptr) {
        task->loop_ptr->killTimer(timer_id);
        timer_id = INVALID_TIMER_ID;
    }
    if (timer) {
        hevent_set_userdata(timer, NULL);
        htimer_del(timer);
        timer = NULL;
    }
    HvJsPromiseOp::cancel(reason);
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
    if (task == NULL) return hvjs_rejected_promise(js, "hv.sleep: no js task");
    if (argc < 1) return hvjs_rejected_promise(js, "hv.sleep: missing timeout");
    int32_t ms = 0;
    if (JS_ToInt32(js, &ms, argv[0]) != 0) return hvjs_rejected_promise(js, "hv.sleep: invalid timeout");

    HvJsSleep* sleep = NULL;
    JSValue promise = hvjs_new_promise<HvJsSleep>(js, task, &sleep);
    if (JS_IsException(promise)) return promise;
    std::shared_ptr<HvJsPromiseOp*> handle = sleep->handle;
    int delay = ms > 0 ? ms : 1;
    if (task->loop_ptr && task->loop_ptr->isRunning()) {
        sleep->timer_id = task->loop_ptr->setTimeout(delay, [handle](TimerID) {
            HvJsPromiseOp* op = handle ? *handle : NULL;
            if (op == NULL) return;
            static_cast<HvJsSleep*>(op)->timer_id = INVALID_TIMER_ID;
            hvjs_promise_resolve(op, JS_UNDEFINED);
        });
    }
    else {
        sleep->timer = htimer_add(task->loop, sleep_timer_cb, (uint32_t)delay, 1);
        if (sleep->timer) hevent_set_userdata(sleep->timer, sleep);
    }
    if (sleep->timer == NULL && sleep->timer_id == INVALID_TIMER_ID) {
        hvjs_promise_reject(sleep, "hv.sleep: failed to create timer");
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

JSValue js_print(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) fputc(' ', stdout);
        std::string s = hvjs_to_string(js, argv[i]);
        fputs(s.c_str(), stdout);
    }
    fputc('\n', stdout);
    return JS_UNDEFINED;
}

void set_script_args(JSContext* js, int argc, char** argv) {
    JSValue arr = JS_NewArray(js);
    for (int i = 1; i < argc; ++i) {
        JS_SetPropertyUint32(js, arr, i - 1, JS_NewString(js, argv[i]));
    }
    JSValue global = JS_GetGlobalObject(js);
    JS_SetPropertyStr(js, global, "arg", arr);
    JS_FreeValue(js, global);
}

void script_finish(HvJsTask* base, JSValue result) {
    HvJsScriptTask* task = static_cast<HvJsScriptTask*>(base);
    if (task->finished) return;
    task->finished = true;
    if (!task->error.empty()) {
        hloge("[js] script error: %s", task->error.c_str());
        if (task->exit_code) *task->exit_code = 1;
    }
    else if (task->promise_rejected) {
        std::string err = hvjs_to_string(task->js, result);
        hloge("[js] script rejected: %s", err.c_str());
        task->error = err.empty() ? "javascript rejection" : err;
        if (task->exit_code) *task->exit_code = 1;
    }
    JS_FreeValue(task->js, result);
    hvjs_task_cancel_timeout(task);
    if (task->loop_ptr && task->loop_ptr->isRunning()) {
        task->loop_ptr->stop();
    }
    else if (task->loop && hloop_status(task->loop) == HLOOP_STATUS_RUNNING) {
        hloop_stop(task->loop);
    }
    hvjs_task_unref(task);
}

bool load_file(const char* filepath, std::string* out) {
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

JSValue require_hv(JSContext* js) {
    JSValue hv = JS_NewObject(js);
    JS_SetPropertyStr(js, hv, "version", JS_NewCFunction(js, js_hv_version, "version", 0));
    JS_SetPropertyStr(js, hv, "log", JS_NewCFunction(js, js_hv_log, "log", 1));
    JS_SetPropertyStr(js, hv, "sleep", JS_NewCFunction(js, js_hv_sleep, "sleep", 1));
    return hv;
}

} // namespace

int hvjs_dostring(hloop_t* loop, const char* code, const char* filename, int argc, char** argv, int* exit_code) {
    if (loop == NULL || code == NULL) return -1;

    HvJsScriptTask* task = new HvJsScriptTask();
    task->exit_code = exit_code;
    task->loop = loop;
    task->loop_ptr = currentThreadEventLoopPtr;
    if (task->loop_ptr == NULL) {
        EventLoop* current_loop = currentThreadEventLoop;
        if (current_loop && current_loop->loop() == loop) {
            task->loop_ptr = current_loop->shared_from_this();
        }
    }
    task->finish = script_finish;
    hvjs_task_set_runtime(task, hvjs_runtime(loop));
    task->js = task->runtime ? JS_NewContext(task->runtime->rt) : NULL;
    if (task->runtime == NULL || task->js == NULL) {
        hloge("[js] failed to create quickjs runtime");
        hvjs_task_unref(task);
        return -1;
    }
    JS_SetContextOpaque(task->js, task);
    task->timeout_ms = DEFAULT_JS_TASK_TIMEOUT;
    task->start_hrtime = gethrtime_us();
    if (!hvjs_task_start_timeout(task, task->timeout_ms)) {
        hloge("[js] failed to create timeout timer");
        hvjs_task_unref(task);
        return -1;
    }
    set_script_args(task->js, argc, argv);

    std::string err;
    {
        HvJsTaskScope scope(task);
        JSValue global = JS_GetGlobalObject(task->js);
        JS_SetPropertyStr(task->js, global, "require", JS_NewCFunction(task->js, hvjs_require, "require", 1));
        JS_SetPropertyStr(task->js, global, "print", JS_NewCFunction(task->js, js_print, "print", 1));

        std::string wrapped = "(async function(){\n";
        wrapped += code;
        wrapped += "\n})()";
        JSValue eval = JS_Eval(task->js, wrapped.c_str(), wrapped.size(), filename ? filename : "<input>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(eval)) {
            err = hvjs_exception_string(task->js);
            JS_FreeValue(task->js, global);
            hloge("[js] eval %s failed: %s", filename ? filename : "<input>", err.c_str());
            task->closing = true;
            hvjs_task_cancel_ops(task, "javascript script error");
            hvjs_task_cancel_timeout(task);
            hvjs_task_unref(task);
            return -1;
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
            err = hvjs_exception_string(task->js);
            hloge("[js] Promise.resolve %s failed: %s", filename ? filename : "<input>", err.c_str());
            task->closing = true;
            hvjs_task_cancel_ops(task, "javascript script error");
            hvjs_task_cancel_timeout(task);
            hvjs_task_unref(task);
            return -1;
        }
        if (!hvjs_watch_promise(task, &err)) {
            hloge("[js] watch promise %s failed: %s", filename ? filename : "<input>", err.c_str());
            task->closing = true;
            hvjs_task_cancel_ops(task, "javascript script error");
            hvjs_task_cancel_timeout(task);
            hvjs_task_unref(task);
            return -1;
        }
    }

    hvjs_task_ref(task);
    hvjs_drain_jobs(task);
    bool finished = task->finished;
    bool ok = task->error.empty() && !task->promise_rejected;
    hvjs_task_unref(task);
    return finished ? (ok ? 1 : -1) : 0;
}

int hvjs_dofile(hloop_t* loop, const char* filepath, int argc, char** argv, int* exit_code) {
    if (filepath == NULL) return -1;
    std::string code;
    if (!load_file(filepath, &code)) {
        hloge("[js] failed to read %s", filepath);
        return -1;
    }
    return hvjs_dostring(loop, code.c_str(), filepath, argc, argv, exit_code);
}

HvJsRuntime::HvJsRuntime() : rt(NULL), current_task(NULL), tasks() {}

HvJsTaskScope::HvJsTaskScope(HvJsTask* task) : runtime(task ? task->runtime : NULL), current(task), previous(runtime ? runtime->current_task : NULL) {
    if (runtime) {
        runtime->current_task = task;
    }
}

HvJsTaskScope::~HvJsTaskScope() {
    if (runtime && runtime->current_task == current) {
        runtime->current_task = previous;
    }
}

HvJsTask::HvJsTask()
    : runtime(NULL), js(NULL), loop(NULL), loop_ptr(), promise(JS_UNDEFINED), promise_result(JS_UNDEFINED), promise_settled(false), promise_rejected(false),
      finished(false), drain_scheduled(false), in_call(0), closing(false), refcount(1), start_hrtime(0), timeout_ms(0), timeout_timer_id(INVALID_TIMER_ID),
      timeout_timer(NULL), finish(NULL) {}

HvJsTask::~HvJsTask() {}

HvJsPromiseOp::HvJsPromiseOp()
    : task(NULL), resolve(JS_UNDEFINED), reject(JS_UNDEFINED), completed(false), defer_delete(false), handle(std::make_shared<HvJsPromiseOp*>(this)) {}

HvJsPromiseOp::~HvJsPromiseOp() {}

void HvJsPromiseOp::cancel(const char* reason) {
    (void)reason;
}

HvJsRuntime* hvjs_runtime(hloop_t* loop) {
    if (loop == NULL) return NULL;
    HvJsRuntime* runtime = (HvJsRuntime*)hloop_js_runtime(loop);
    if (runtime) return runtime;

    runtime = new HvJsRuntime();
    runtime->rt = JS_NewRuntime();
    if (runtime->rt == NULL) {
        delete runtime;
        return NULL;
    }
    JS_SetMemoryLimit(runtime->rt, DEFAULT_JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(runtime->rt, DEFAULT_JS_STACK_SIZE);
    JS_SetRuntimeOpaque(runtime->rt, runtime);
    JS_SetInterruptHandler(runtime->rt, interrupt_handler, NULL);
    hloop_set_js_runtime(loop, runtime, runtime_dtor);
    return runtime;
}

void hvjs_task_ref(HvJsTask* task) {
    if (task == NULL) return;
    ++task->refcount;
}

void hvjs_task_unref(HvJsTask* task) {
    if (task == NULL) return;
    if (--task->refcount != 0) return;
    task->closing = true;
    if (task->runtime && task->runtime->current_task == task) {
        task->runtime->current_task = NULL;
    }
    if (task->runtime) {
        auto iter = std::find(task->runtime->tasks.begin(), task->runtime->tasks.end(), task);
        if (iter != task->runtime->tasks.end()) {
            task->runtime->tasks.erase(iter);
        }
    }
    finish_deferred_ops(task);
    if (task->js && !JS_IsUndefined(task->promise_result)) {
        JS_FreeValue(task->js, task->promise_result);
        task->promise_result = JS_UNDEFINED;
    }
    if (task->js && !JS_IsUndefined(task->promise)) {
        JS_FreeValue(task->js, task->promise);
        task->promise = JS_UNDEFINED;
    }
    if (task->js) {
        if (task->runtime && task->runtime->rt) {
            JS_RunGC(task->runtime->rt);
        }
        JS_FreeContext(task->js);
        task->js = NULL;
    }
    delete task;
}

void hvjs_task_set_runtime(HvJsTask* task, HvJsRuntime* runtime) {
    if (task == NULL) return;
    task->runtime = runtime;
    if (runtime) {
        runtime->tasks.push_back(task);
    }
}

bool hvjs_task_start_timeout(HvJsTask* task, int timeout_ms) {
    if (task == NULL || timeout_ms <= 0) return true;
    task->timeout_ms = timeout_ms;
    if (task->start_hrtime == 0) {
        task->start_hrtime = gethrtime_us();
    }
    hvjs_task_ref(task);
    if (task->loop_ptr && task->loop_ptr->isRunning()) {
        task->timeout_timer_id = task->loop_ptr->setTimeout(timeout_ms, [task](TimerID timerID) {
            if (task->timeout_timer_id != timerID) return;
            task->timeout_timer_id = INVALID_TIMER_ID;
            if (!task->finished && !task->closing) {
                task->error = "javascript request timeout";
                task->closing = true;
                hvjs_task_cancel_ops(task, task->error.c_str());
                if (task->finish) {
                    task->finish(task, JS_UNDEFINED);
                }
            }
            hvjs_task_unref(task);
        });
        if (task->timeout_timer_id == INVALID_TIMER_ID) {
            hvjs_task_unref(task);
            return false;
        }
        return true;
    }
    if (task->loop) {
        task->timeout_timer = htimer_add(task->loop, task_timeout_timer_cb, (uint32_t)timeout_ms, 1);
        if (task->timeout_timer) {
            hevent_set_userdata(task->timeout_timer, task);
            return true;
        }
    }
    hvjs_task_unref(task);
    return false;
}

void hvjs_task_cancel_timeout(HvJsTask* task) {
    if (task == NULL) return;
    if (task->timeout_timer_id != INVALID_TIMER_ID && task->loop_ptr) {
        task->loop_ptr->killTimer(task->timeout_timer_id);
        task->timeout_timer_id = INVALID_TIMER_ID;
        hvjs_task_unref(task);
    }
    if (task->timeout_timer) {
        htimer_t* timer = task->timeout_timer;
        hevent_set_userdata(timer, NULL);
        htimer_del(timer);
        task->timeout_timer = NULL;
        hvjs_task_unref(task);
    }
}

void hvjs_task_add_op(HvJsTask* task, HvJsPromiseOp* op) {
    if (task == NULL || op == NULL) return;
    task->ops.push_back(op);
}

void hvjs_task_remove_op(HvJsTask* task, HvJsPromiseOp* op) {
    if (task == NULL || op == NULL) return;
    auto iter = std::find(task->ops.begin(), task->ops.end(), op);
    if (iter != task->ops.end()) {
        task->ops.erase(iter);
    }
}

void hvjs_task_cancel_ops(HvJsTask* task, const char* message) {
    if (task == NULL) return;
    std::vector<HvJsPromiseOp*> ops;
    ops.swap(task->ops);
    for (size_t i = 0; i < ops.size(); ++i) {
        HvJsPromiseOp* op = ops[i];
        if (op == NULL || op->completed) continue;
        cancel_op(op, message);
    }
    finish_deferred_ops(task);
}

void hvjs_schedule_drain(HvJsTask* task) {
    if (task == NULL || task->closing) return;
    if (task->drain_scheduled) return;
    task->drain_scheduled = true;
    hvjs_task_ref(task);
    if (task->loop_ptr && task->loop_ptr->loop() && hloop_status(task->loop_ptr->loop()) == HLOOP_STATUS_RUNNING) {
        task->loop_ptr->queueInLoop([task]() {
            task->drain_scheduled = false;
            hvjs_drain_jobs(task);
            hvjs_task_unref(task);
        });
    }
    else if (task->loop && hloop_status(task->loop) == HLOOP_STATUS_RUNNING) {
        hevent_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.cb = drain_event_cb;
        ev.userdata = task;
        hloop_post_event(task->loop, &ev);
    }
    else {
        task->drain_scheduled = false;
        hvjs_task_unref(task);
    }
}

bool hvjs_watch_promise(HvJsTask* task, std::string* err) {
    if (task == NULL || task->js == NULL || JS_IsUndefined(task->promise)) return false;
    JSContext* js = task->js;
    HvJsTaskScope scope(task);
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
    if (task == NULL || task->finished || task->js == NULL || task->runtime == NULL || task->runtime->rt == NULL) return;
    HvJsTaskScope scope(task);
    JSContext* job_ctx = NULL;
    JSRuntime* rt = task->runtime->rt;
    while (JS_IsJobPending(rt)) {
        int rc = JS_ExecutePendingJob(rt, &job_ctx);
        if (rc < 0) {
            HvJsTask* job_task = job_ctx ? hvjs_get_task(job_ctx) : task;
            if (job_task == NULL) job_task = task;
            job_task->error = hvjs_exception_string(job_ctx ? job_ctx : task->js);
            break;
        }
    }

    HvJsRuntime* runtime = task->runtime;
    std::vector<HvJsTask*> tasks = runtime->tasks;
    for (size_t i = 0; i < tasks.size(); ++i) {
        finish_ready_task(tasks[i]);
    }
}

void hvjs_promise_resolve(HvJsPromiseOp* op, JSValue value) {
    promise_complete(op, value, true);
}

void hvjs_promise_reject(HvJsPromiseOp* op, const char* message) {
    if (op == NULL || op->task == NULL || op->task->js == NULL) return;
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
    if (op == NULL) return;
    HvJsTask* task = op->task;
    if (task && task->in_call == 0) {
        finish_deferred_ops(task);
    }
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
