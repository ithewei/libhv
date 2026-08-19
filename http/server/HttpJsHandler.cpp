#ifdef WITH_JS

#include "HttpJsHandler.h"

#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <quickjs.h>

#include "EventLoop.h"
#include "hfile.h"
#include "hlog.h"
#include "hpath.h"
#include "hstring.h"
#include "htime.h"
#include "hversion.h"
#ifdef HVJS_WITH_HTTP
#include "AsyncHttpClient.h"
#include "WebSocketClient.h"
#endif
#ifdef HVJS_WITH_REDIS
#include "AsyncRedisClient.h"
#endif
#ifdef HVJS_WITH_MQTT
#include "mqtt_client.h"
#endif

namespace hv {

namespace {

struct JsHttpTask;

static const int JS_HTTP_METHOD_REQUEST = -1;

struct JsHttpTask {
    JSRuntime* rt;
    JSContext* js;
    hloop_t* loop;
    EventLoopPtr loop_ptr;
    HttpContextPtr ctx;
    JSValue promise;
    bool async;
    bool finished;
    bool in_call;
    bool closing;
    int refcount;
    std::string error;

    JsHttpTask() : rt(NULL), js(NULL), loop(NULL), promise(JS_UNDEFINED), async(false), finished(false), in_call(false), closing(false), refcount(1) {}
};

struct JsPromiseOp {
    JsHttpTask* task;
    JSValue resolve;
    JSValue reject;
    bool completed;
    bool defer_delete;

    JsPromiseOp() : task(NULL), resolve(JS_UNDEFINED), reject(JS_UNDEFINED), completed(false), defer_delete(false) {}

    virtual ~JsPromiseOp() {}
};

struct JsSleep : public JsPromiseOp {
    htimer_t* timer;
    TimerID timer_id;

    JsSleep() : timer(NULL), timer_id(INVALID_TIMER_ID) {}
};

struct JsImmediatePromise : public JsPromiseOp {};

static std::mutex& js_class_id_mutex() {
    static std::mutex mutex;
    return mutex;
}

static void js_new_class_id(JSClassID* class_id) {
    std::lock_guard<std::mutex> lock(js_class_id_mutex());
    JS_NewClassID(class_id);
}

static void task_ref(JsHttpTask* task) {
    ++task->refcount;
}

static void task_unref(JsHttpTask* task) {
    if (--task->refcount != 0) return;
    task->closing = true;
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

static void drain_jobs(JsHttpTask* task);
static std::string js_to_string(JSContext* ctx, JSValueConst value);
static std::string js_exception_string(JSContext* ctx);
static void js_promise_complete(JsPromiseOp* op, JSValue value, bool ok);

static void drain_event_cb(hevent_t* ev) {
    JsHttpTask* task = (JsHttpTask*)hevent_userdata(ev);
    drain_jobs(task);
    task_unref(task);
}

static void schedule_drain(JsHttpTask* task) {
    if (task == NULL || task->closing) return;
    task_ref(task);
    if (task->loop_ptr) {
        task->loop_ptr->queueInLoop([task]() {
            drain_jobs(task);
            task_unref(task);
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
        task_unref(task);
    }
}

template <typename T> static JSValue js_new_promise(JSContext* js, JsHttpTask* task, T** out) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(js, funcs);
    if (JS_IsException(promise)) return promise;
    T* op = new T();
    op->task = task;
    op->resolve = funcs[0];
    op->reject = funcs[1];
    task_ref(task);
    *out = op;
    return promise;
}

static void js_promise_complete(JsPromiseOp* op, JSValue value, bool ok) {
    JsHttpTask* task = op->task;
    if (op->completed) {
        JS_FreeValue(task->js, value);
        return;
    }
    op->completed = true;
    if (!task->closing) {
        JSValue func = ok ? op->resolve : op->reject;
        JSValue ret = JS_Call(task->js, func, JS_UNDEFINED, 1, &value);
        if (JS_IsException(ret) && task->error.empty()) {
            task->error = js_exception_string(task->js);
        }
        JS_FreeValue(task->js, ret);
        JS_FreeValue(task->js, value);
        JS_FreeValue(task->js, op->resolve);
        JS_FreeValue(task->js, op->reject);
        op->resolve = JS_UNDEFINED;
        op->reject = JS_UNDEFINED;
        if (task->in_call) {
            op->defer_delete = true;
            schedule_drain(task);
            return;
        }
        schedule_drain(task);
    }
    else {
        JS_FreeValue(task->js, value);
        JS_FreeValue(task->js, op->resolve);
        JS_FreeValue(task->js, op->reject);
        op->resolve = JS_UNDEFINED;
        op->reject = JS_UNDEFINED;
    }
    delete op;
    task_unref(task);
}

static void js_promise_resolve(JsPromiseOp* op, JSValue value) {
    js_promise_complete(op, value, true);
}

static void js_promise_reject(JsPromiseOp* op, const char* message) {
    js_promise_complete(op, JS_NewString(op->task->js, message ? message : "error"), false);
}

static JSValue js_rejected_promise(JSContext* js, const char* message) {
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

static JSValue js_async_resolved_promise(JSContext* js, JsHttpTask* task, JSValue value) {
    if (task == NULL) {
        JS_FreeValue(js, value);
        return JS_ThrowInternalError(js, "invalid HttpJsHandler task");
    }
    JsImmediatePromise* op = NULL;
    JSValue promise = js_new_promise<JsImmediatePromise>(js, task, &op);
    if (JS_IsException(promise)) {
        JS_FreeValue(js, value);
        return promise;
    }
    js_promise_resolve(op, value);
    return promise;
}

static void js_finish_deferred_op(JsPromiseOp* op) {
    if (op == NULL || !op->completed || !op->defer_delete) return;
    JsHttpTask* task = op->task;
    delete op;
    task_unref(task);
}

static std::string js_to_string(JSContext* ctx, JSValueConst value) {
    size_t len = 0;
    const char* str = JS_ToCStringLen(ctx, &len, value);
    if (str == NULL) return std::string();
    std::string out(str, len);
    JS_FreeCString(ctx, str);
    return out;
}

static bool js_get_property(JSContext* js, JSValueConst obj, const char* name, JSValue* out) {
    *out = JS_UNDEFINED;
    if (!JS_IsObject(obj)) return false;
    *out = JS_GetPropertyStr(js, obj, name);
    return !JS_IsUndefined(*out) && !JS_IsException(*out);
}

static std::string js_get_string_property(JSContext* js, JSValueConst obj, const char* name, const char* defvalue = "") {
    JSValue value;
    if (!js_get_property(js, obj, name, &value) || JS_IsNull(value)) {
        if (!JS_IsUndefined(value) && !JS_IsException(value)) JS_FreeValue(js, value);
        return defvalue;
    }
    std::string out = js_to_string(js, value);
    JS_FreeValue(js, value);
    return out;
}

static int js_get_int_property(JSContext* js, JSValueConst obj, const char* name, int defvalue = 0) {
    JSValue value;
    if (!js_get_property(js, obj, name, &value) || JS_IsNull(value)) {
        if (!JS_IsUndefined(value) && !JS_IsException(value)) JS_FreeValue(js, value);
        return defvalue;
    }
    int32_t out = defvalue;
    JS_ToInt32(js, &out, value);
    JS_FreeValue(js, value);
    return out;
}

static bool js_get_bool_property(JSContext* js, JSValueConst obj, const char* name, bool defvalue = false) {
    JSValue value;
    if (!js_get_property(js, obj, name, &value) || JS_IsNull(value)) {
        if (!JS_IsUndefined(value) && !JS_IsException(value)) JS_FreeValue(js, value);
        return defvalue;
    }
    bool out = JS_ToBool(js, value) != 0;
    JS_FreeValue(js, value);
    return out;
}

static std::string js_exception_string(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    std::string msg = js_to_string(ctx, exception);
    JS_FreeValue(ctx, exception);
    return msg.empty() ? "javascript exception" : msg;
}

static JsHttpTask* js_get_task(JSContext* js) {
    return (JsHttpTask*)JS_GetContextOpaque(js);
}

static JSValue js_ctx_method(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx || !task->ctx->request) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    return JS_NewString(js, http_method_str(task->ctx->request->method));
}

static JSValue js_ctx_path(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string path = task->ctx->path();
    return JS_NewStringLen(js, path.data(), path.size());
}

static JSValue js_ctx_query(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string key = argc > 0 ? js_to_string(js, argv[0]) : std::string();
    std::string defvalue = argc > 1 ? js_to_string(js, argv[1]) : std::string();
    std::string value = task->ctx->param(key.c_str(), defvalue);
    return JS_NewStringLen(js, value.data(), value.size());
}

static JSValue js_ctx_header(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string key = argc > 0 ? js_to_string(js, argv[0]) : std::string();
    std::string defvalue = argc > 1 ? js_to_string(js, argv[1]) : std::string();
    std::string value = task->ctx->header(key.c_str(), defvalue);
    return JS_NewStringLen(js, value.data(), value.size());
}

static JSValue js_ctx_body(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string& body = task->ctx->body();
    return JS_NewStringLen(js, body.data(), body.size());
}

static JSValue js_ctx_status(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx || argc < 1) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    int32_t status = 0;
    if (JS_ToInt32(js, &status, argv[0]) != 0) return JS_EXCEPTION;
    task->ctx->response->status_code = (http_status)status;
    return JS_NewInt32(js, status);
}

static JSValue js_ctx_set_header(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx || argc < 2) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string key = js_to_string(js, argv[0]);
    std::string value = js_to_string(js, argv[1]);
    task->ctx->setHeader(key.c_str(), value);
    return JS_UNDEFINED;
}

static JSValue js_ctx_text(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx || argc < 1) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string text = js_to_string(js, argv[0]);
    task->ctx->response->String(text);
    return JS_NewInt32(js, task->ctx->response->status_code);
}

static JSValue js_ctx_json(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx || argc < 1) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    JSValue json = JS_JSONStringify(js, argv[0], JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) return json;
    std::string body = js_to_string(js, json);
    JS_FreeValue(js, json);
    task->ctx->response->SetContentType(APPLICATION_JSON);
    task->ctx->response->body = body;
    return JS_NewInt32(js, task->ctx->response->status_code);
}

static JSValue js_new_ctx(JSContext* js, const HttpContextPtr& ctx) {
    (void)ctx;
    JSValue obj = JS_NewObject(js);
    JS_SetPropertyStr(js, obj, "method", JS_NewCFunction(js, js_ctx_method, "method", 0));
    JS_SetPropertyStr(js, obj, "path", JS_NewCFunction(js, js_ctx_path, "path", 0));
    JS_SetPropertyStr(js, obj, "param", JS_NewCFunction(js, js_ctx_query, "param", 1));
    JS_SetPropertyStr(js, obj, "query", JS_NewCFunction(js, js_ctx_query, "query", 1));
    JS_SetPropertyStr(js, obj, "header", JS_NewCFunction(js, js_ctx_header, "header", 1));
    JS_SetPropertyStr(js, obj, "body", JS_NewCFunction(js, js_ctx_body, "body", 0));
    JS_SetPropertyStr(js, obj, "status", JS_NewCFunction(js, js_ctx_status, "status", 1));
    JS_SetPropertyStr(js, obj, "setHeader", JS_NewCFunction(js, js_ctx_set_header, "setHeader", 2));
    JS_SetPropertyStr(js, obj, "set_header", JS_NewCFunction(js, js_ctx_set_header, "set_header", 2));
    JS_SetPropertyStr(js, obj, "text", JS_NewCFunction(js, js_ctx_text, "text", 1));
    JS_SetPropertyStr(js, obj, "json", JS_NewCFunction(js, js_ctx_json, "json", 1));
    return obj;
}

static void task_finish(JsHttpTask* task, JSValue result);

static void drain_jobs(JsHttpTask* task) {
    JSContext* job_ctx = NULL;
    while (JS_IsJobPending(task->rt)) {
        int rc = JS_ExecutePendingJob(task->rt, &job_ctx);
        if (rc < 0) {
            task->error = js_exception_string(job_ctx ? job_ctx : task->js);
            break;
        }
    }
    if (!task->finished && !JS_IsUndefined(task->promise)) {
        JSPromiseStateEnum state = JS_PromiseState(task->js, task->promise);
        if (state != JS_PROMISE_PENDING) {
            JSValue value = JS_PromiseResult(task->js, task->promise);
            task_finish(task, value);
            return;
        }
    }
    if (!task->error.empty()) {
        task_finish(task, JS_UNDEFINED);
    }
}

static void sleep_timer_cb(htimer_t* timer) {
    JsSleep* sleep = (JsSleep*)hevent_userdata(timer);
    js_promise_resolve(sleep, JS_UNDEFINED);
}

static JSValue js_hv_sleep(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = (JsHttpTask*)JS_GetContextOpaque(js);
    if (task == NULL || argc < 1) return JS_EXCEPTION;
    int32_t ms = 0;
    if (JS_ToInt32(js, &ms, argv[0]) != 0) return JS_EXCEPTION;
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(js, funcs);
    if (JS_IsException(promise)) return promise;

    JsSleep* sleep = new JsSleep();
    sleep->task = task;
    sleep->resolve = funcs[0];
    JS_FreeValue(js, funcs[1]);
    task_ref(task);
    if (task->loop_ptr) {
        sleep->timer_id = task->loop_ptr->setTimeout(ms, [sleep](TimerID) { js_promise_resolve(sleep, JS_UNDEFINED); });
    }
    else {
        sleep->timer = htimer_add(task->loop, sleep_timer_cb, (uint32_t)ms, 1);
        if (sleep->timer) hevent_set_userdata(sleep->timer, sleep);
    }
    if (sleep->timer == NULL && sleep->timer_id == INVALID_TIMER_ID) {
        task_unref(task);
        JS_FreeValue(js, sleep->resolve);
        delete sleep;
        JS_FreeValue(js, promise);
        return JS_ThrowInternalError(js, "hv.sleep: failed to create timer");
    }
    return promise;
}

static JSValue js_hv_version(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(js, HV_VERSION_STRING);
}

static JSValue js_hv_log(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) line += "\\t";
        line += js_to_string(js, argv[i]);
    }
    hlogi("%s", line.c_str());
    return JS_UNDEFINED;
}
#ifdef HVJS_WITH_HTTP
struct JsHttpRequest : public JsPromiseOp {
    std::shared_ptr<AsyncHttpClient> client;
};

static JSValue js_push_headers(JSContext* js, const http_headers& headers) {
    JSValue obj = JS_NewObject(js);
    for (auto& kv : headers) {
        JS_SetPropertyStr(js, obj, kv.first.c_str(), JS_NewStringLen(js, kv.second.data(), kv.second.size()));
    }
    return obj;
}

static JSValue js_push_http_response(JSContext* js, const HttpResponsePtr& resp) {
    JSValue obj = JS_NewObject(js);
    JS_SetPropertyStr(js, obj, "status", JS_NewInt32(js, resp ? resp->status_code : 0));
    if (resp) {
        JS_SetPropertyStr(js, obj, "body", JS_NewStringLen(js, resp->body.data(), resp->body.size()));
        JS_SetPropertyStr(js, obj, "headers", js_push_headers(js, resp->headers));
    }
    else {
        JS_SetPropertyStr(js, obj, "body", JS_NewString(js, ""));
        JS_SetPropertyStr(js, obj, "headers", JS_NewObject(js));
    }
    return obj;
}

static int js_fill_http_request(JSContext* js, JSValueConst* argv, int argc, http_method method, int url_index, HttpRequestPtr* out) {
    if (argc <= url_index) {
        JS_ThrowTypeError(js, "missing url");
        return -1;
    }
    std::string url = js_to_string(js, argv[url_index]);
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    if (argc > url_index + 1 && !JS_IsUndefined(argv[url_index + 1]) && !JS_IsNull(argv[url_index + 1])) {
        std::string body = js_to_string(js, argv[url_index + 1]);
        req->body = body;
    }
    if (argc > url_index + 2 && JS_IsObject(argv[url_index + 2])) {
        JSPropertyEnum* tab = NULL;
        uint32_t len = 0;
        if (JS_GetOwnPropertyNames(js, &tab, &len, argv[url_index + 2], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < len; ++i) {
                JSValue key = JS_AtomToString(js, tab[i].atom);
                JSValue value = JS_GetProperty(js, argv[url_index + 2], tab[i].atom);
                std::string k = js_to_string(js, key);
                std::string v = js_to_string(js, value);
                if (!k.empty()) req->headers[k] = v;
                JS_FreeValue(js, value);
                JS_FreeValue(js, key);
            }
            JS_FreePropertyEnum(js, tab, len);
        }
    }
    *out = req;
    return 0;
}

static JSValue js_http_request(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->loop_ptr) {
        return js_rejected_promise(js, "hv.http: no shared event loop on this thread");
    }
    http_method method = (http_method)magic;
    int url_index = 0;
    if (magic == JS_HTTP_METHOD_REQUEST) {
        if (argc < 2) return js_rejected_promise(js, "hv.http: request needs method and url");
        std::string m = js_to_string(js, argv[0]);
        toupper(m);
        method = http_method_enum(m.c_str());
        url_index = 1;
    }
    if (method == HTTP_CUSTOM_METHOD) {
        return js_rejected_promise(js, "hv.http: unsupported method");
    }

    HttpRequestPtr req;
    if (js_fill_http_request(js, argv, argc, method, url_index, &req) != 0) {
        return JS_EXCEPTION;
    }

    JsHttpRequest* op = NULL;
    JSValue promise = js_new_promise<JsHttpRequest>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->client = std::make_shared<AsyncHttpClient>(task->loop_ptr);
    std::shared_ptr<AsyncHttpClient> client = op->client;
    task->in_call = true;
    int ret = client->send(req, [op, client](const HttpResponsePtr& resp) {
        if (op->task->loop_ptr) {
            op->task->loop_ptr->queueInLoop([client]() {});
        }
        JSContext* js = op->task->js;
        if (resp) {
            js_promise_resolve(op, js_push_http_response(js, resp));
        }
        else {
            js_promise_reject(op, "hv.http: request failed");
        }
    });
    if (ret != 0) {
        js_promise_reject(op, "hv.http: request failed");
    }
    task->in_call = false;
    js_finish_deferred_op(op);
    return promise;
}

static JSValue js_require_http(JSContext* js) {
    JSValue http = JS_NewObject(js);
    JS_SetPropertyStr(js, http, "request", JS_NewCFunctionMagic(js, js_http_request, "request", 2, JS_CFUNC_generic_magic, JS_HTTP_METHOD_REQUEST));
    JS_SetPropertyStr(js, http, "get", JS_NewCFunctionMagic(js, js_http_request, "get", 1, JS_CFUNC_generic_magic, HTTP_GET));
    JS_SetPropertyStr(js, http, "post", JS_NewCFunctionMagic(js, js_http_request, "post", 2, JS_CFUNC_generic_magic, HTTP_POST));
    JS_SetPropertyStr(js, http, "put", JS_NewCFunctionMagic(js, js_http_request, "put", 2, JS_CFUNC_generic_magic, HTTP_PUT));
    JS_SetPropertyStr(js, http, "delete", JS_NewCFunctionMagic(js, js_http_request, "delete", 1, JS_CFUNC_generic_magic, HTTP_DELETE));
    return http;
}
#endif
#ifdef HVJS_WITH_REDIS
static JSClassID s_redis_class_id;
static std::once_flag s_redis_class_once;

struct JsRedisState {
    std::shared_ptr<AsyncRedisClient> client;
    bool destroyed;

    JsRedisState() : destroyed(false) {}

    ~JsRedisState() {
        destroyed = true;
        if (client) {
            client->stop(true);
            client.reset();
        }
    }
};

struct JsRedisClient {
    std::shared_ptr<JsRedisState> state;
};

struct JsRedisCommand : public JsPromiseOp {
    std::shared_ptr<JsRedisState> redis;
};

static void js_redis_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    JsRedisClient* box = (JsRedisClient*)JS_GetOpaque(val, s_redis_class_id);
    if (box) {
        delete box;
    }
}

static JsRedisClient* js_redis_client(JSContext* js, JSValueConst this_val) {
    JsRedisClient* box = (JsRedisClient*)JS_GetOpaque2(js, this_val, s_redis_class_id);
    return box;
}

static void js_redis_register_class(JSContext* js) {
    std::call_once(s_redis_class_once, []() { js_new_class_id(&s_redis_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_redis_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.redis.client";
        def.finalizer = js_redis_finalizer;
        JS_NewClass(rt, s_redis_class_id, &def);
    }
}

static JSValue js_push_redis_reply(JSContext* js, const RedisReply& reply) {
    switch (reply.type) {
    case REDIS_REPLY_STRING: return JS_NewStringLen(js, reply.str.data(), reply.str.size());
    case REDIS_REPLY_INTEGER: return JS_NewInt64(js, reply.integer);
    case REDIS_REPLY_ARRAY: {
        if (reply.null_array) return JS_NULL;
        JSValue arr = JS_NewArray(js);
        for (uint32_t i = 0; i < reply.elements.size(); ++i) {
            JSValue item = reply.elements[i].isNil() ? JS_NULL : js_push_redis_reply(js, reply.elements[i]);
            JS_SetPropertyUint32(js, arr, i, item);
        }
        return arr;
    }
    case REDIS_REPLY_NIL:
    default: return JS_NULL;
    }
}

static void js_redis_resolve_result(JsRedisCommand* op, const RedisResult& result) {
    JSContext* js = op->task->js;
    if (!op->redis || op->redis->destroyed) {
        js_promise_reject(op, "hv.redis: client closed");
        return;
    }
    if (result.code != 0) {
        char err[64];
        snprintf(err, sizeof(err), "hv.redis: request failed (%d)", result.code);
        js_promise_reject(op, err);
        return;
    }
    if (result.reply.isError()) {
        js_promise_reject(op, result.reply.error().c_str());
        return;
    }
    js_promise_resolve(op, js_push_redis_reply(js, result.reply));
}

static bool js_build_redis_command(JSContext* js, JSValueConst* argv, int argc, int first, RedisCommand* cmd) {
    if (argc <= first) return false;
    if (JS_IsArray(js, argv[first]) && argc == first + 1) {
        JSValue lenv = JS_GetPropertyStr(js, argv[first], "length");
        uint32_t len = 0;
        JS_ToUint32(js, &len, lenv);
        JS_FreeValue(js, lenv);
        for (uint32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(js, argv[first], i);
            cmd->push_back(js_to_string(js, item));
            JS_FreeValue(js, item);
        }
    }
    else {
        for (int i = first; i < argc; ++i) {
            cmd->push_back(js_to_string(js, argv[i]));
        }
    }
    return !cmd->empty();
}

static const char* js_redis_verb_name(int magic) {
    switch (magic) {
    case 1: return "GET";
    case 2: return "SET";
    case 3: return "DEL";
    case 4: return "INCR";
    case 5: return "DECR";
    case 6: return "EXPIRE";
    case 7: return "EXISTS";
    default: return NULL;
    }
}

static JSValue js_redis_command(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    JsRedisClient* box = js_redis_client(js, this_val);
    JsRedisState* state = box ? box->state.get() : NULL;
    if (state == NULL || !state->client || state->destroyed) {
        return js_rejected_promise(js, "hv.redis: client closed");
    }
    RedisCommand cmd;
    if (magic != 0) {
        const char* verb = js_redis_verb_name(magic);
        if (verb == NULL) {
            return js_rejected_promise(js, "hv.redis: unknown command");
        }
        cmd.push_back(verb);
        for (int i = 0; i < argc; ++i) {
            cmd.push_back(js_to_string(js, argv[i]));
        }
    }
    else if (!js_build_redis_command(js, argv, argc, 0, &cmd)) {
        return js_rejected_promise(js, "hv.redis: empty or invalid command");
    }

    JsHttpTask* task = js_get_task(js);
    JsRedisCommand* op = NULL;
    JSValue promise = js_new_promise<JsRedisCommand>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->redis = box->state;
    task->in_call = true;
    int ret = state->client->command(cmd, [op](const RedisResult& result) { js_redis_resolve_result(op, result); });
    if (ret != 0) {
        js_promise_reject(op, "hv.redis: request failed");
    }
    task->in_call = false;
    js_finish_deferred_op(op);
    return promise;
}

static JSValue js_redis_new(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->loop_ptr) {
        return JS_ThrowTypeError(js, "hv.redis: no shared event loop on this thread");
    }
    js_redis_register_class(js);

    std::string host = "127.0.0.1";
    int port = 6379;
    std::string auth;
    int db = 0;
    int timeout = 0;
    if (argc > 0 && JS_IsObject(argv[0])) {
        host = js_get_string_property(js, argv[0], "host", "127.0.0.1");
        port = js_get_int_property(js, argv[0], "port", 6379);
        auth = js_get_string_property(js, argv[0], "auth", "");
        db = js_get_int_property(js, argv[0], "db", 0);
        timeout = js_get_int_property(js, argv[0], "timeout", 0);
    }

    JSValue obj = JS_NewObjectClass(js, s_redis_class_id);
    if (JS_IsException(obj)) return obj;
    JsRedisClient* box = new JsRedisClient();
    box->state = std::make_shared<JsRedisState>();
    box->state->client = std::make_shared<AsyncRedisClient>(task->loop_ptr);
    box->state->client->setHost(host);
    box->state->client->setPort(port);
    if (!auth.empty()) box->state->client->setAuth(auth);
    if (db > 0) box->state->client->setDb(db);
    if (timeout > 0) box->state->client->setTimeout(timeout);
    box->state->client->start(false);
    JS_SetOpaque(obj, box);

    JS_SetPropertyStr(js, obj, "command", JS_NewCFunctionMagic(js, js_redis_command, "command", 1, JS_CFUNC_generic_magic, 0));
    static const char* verbs[] = {"GET", "SET", "DEL", "INCR", "DECR", "EXPIRE", "EXISTS", NULL};
    for (int i = 0; verbs[i]; ++i) {
        std::string name = verbs[i];
        for (char& c : name) c = (char)::tolower((unsigned char)c);
        JS_SetPropertyStr(js, obj, name.c_str(), JS_NewCFunctionMagic(js, js_redis_command, name.c_str(), 1, JS_CFUNC_generic_magic, i + 1));
    }
    return obj;
}

static JSValue js_require_redis(JSContext* js) {
    JSValue redis = JS_NewObject(js);
    JS_SetPropertyStr(js, redis, "new", JS_NewCFunction(js, js_redis_new, "new", 1));
    return redis;
}
#endif
#ifdef HVJS_WITH_HTTP
static JSClassID s_ws_class_id;
static std::once_flag s_ws_class_once;

struct JsWsState {
    std::shared_ptr<WebSocketClient> client;
    std::deque<std::string> inbox;
    JsPromiseOp* connect_op;
    JsPromiseOp* recv_op;
    bool js_alive;
    bool connected;
    bool closed;

    JsWsState() : connect_op(NULL), recv_op(NULL), js_alive(false), connected(false), closed(false) {}

    void detach() {
        closed = true;
        connected = false;
        if (client) {
            client->onopen = NULL;
            client->onmessage = NULL;
            client->onclose = NULL;
            client->close();
            client.reset();
        }
    }

    ~JsWsState() { detach(); }
};

struct JsWsClient {
    std::shared_ptr<JsWsState> state;
};

struct JsWsConnect : public JsPromiseOp {
    std::shared_ptr<JsWsState> state;
};

struct JsWsRecv : public JsPromiseOp {
    std::shared_ptr<JsWsState> state;
};

static JsWsClient* js_ws_client(JSContext* js, JSValueConst this_val) {
    return (JsWsClient*)JS_GetOpaque2(js, this_val, s_ws_class_id);
}

static void js_ws_detach_after_callback(const EventLoopPtr& loop, const std::shared_ptr<JsWsState>& state) {
    if (!state) return;
    if (loop) {
        loop->queueInLoop([state]() { state->detach(); });
    }
    else {
        state->detach();
    }
}

static void js_ws_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    JsWsClient* box = (JsWsClient*)JS_GetOpaque(val, s_ws_class_id);
    if (box && box->state) {
        box->state->js_alive = false;
        if (box->state->connect_op == NULL && box->state->recv_op == NULL) {
            box->state->detach();
        }
    }
    delete box;
}

static void js_ws_register_class(JSContext* js) {
    std::call_once(s_ws_class_once, []() { js_new_class_id(&s_ws_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_ws_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.ws.client";
        def.finalizer = js_ws_finalizer;
        JS_NewClass(rt, s_ws_class_id, &def);
    }
}

static void js_ws_try_deliver(const std::shared_ptr<JsWsState>& state) {
    if (!state || state->recv_op == NULL) return;
    JsWsRecv* op = static_cast<JsWsRecv*>(state->recv_op);
    std::shared_ptr<JsWsState> hold = op->state;
    EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
    if (!state->inbox.empty()) {
        std::string msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        state->recv_op = NULL;
        js_promise_resolve(op, JS_NewStringLen(op->task->js, msg.data(), msg.size()));
    }
    else if (state->closed) {
        state->recv_op = NULL;
        js_promise_reject(op, "closed");
    }
    if (!hold->js_alive && hold->connect_op == NULL && hold->recv_op == NULL) {
        js_ws_detach_after_callback(loop, hold);
    }
}

static JSValue js_ws_send(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsWsClient* box = js_ws_client(js, this_val);
    JsWsState* state = box ? box->state.get() : NULL;
    if (state == NULL || !state->client || !state->connected) {
        return JS_ThrowTypeError(js, "hv.ws: closed");
    }
    std::string msg = argc > 0 ? js_to_string(js, argv[0]) : std::string();
    enum ws_opcode opcode = WS_OPCODE_TEXT;
    if (argc > 1 && js_to_string(js, argv[1]) == "binary") {
        opcode = WS_OPCODE_BINARY;
    }
    int ret = state->client->send(msg.data(), (int)msg.size(), opcode);
    if (ret < 0) {
        return JS_ThrowInternalError(js, "hv.ws: send failed");
    }
    return JS_NewInt32(js, ret);
}

static JSValue js_ws_recv(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    JsWsClient* box = js_ws_client(js, this_val);
    JsWsState* state = box ? box->state.get() : NULL;
    if (state == NULL || !state->client) {
        return js_rejected_promise(js, "closed");
    }
    if (!state->inbox.empty()) {
        std::string msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        return js_async_resolved_promise(js, js_get_task(js), JS_NewStringLen(js, msg.data(), msg.size()));
    }
    if (state->closed || !state->connected) {
        return js_rejected_promise(js, "closed");
    }
    if (state->recv_op != NULL) {
        return js_rejected_promise(js, "hv.ws: recv already pending");
    }
    JsHttpTask* task = js_get_task(js);
    JsWsRecv* op = NULL;
    JSValue promise = js_new_promise<JsWsRecv>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->state = box->state;
    state->recv_op = op;
    return promise;
}

static JSValue js_ws_close(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    JsWsClient* box = js_ws_client(js, this_val);
    if (box && box->state) {
        std::shared_ptr<JsWsState> state = box->state;
        if (state->connect_op) {
            JsPromiseOp* op = state->connect_op;
            state->connect_op = NULL;
            js_promise_reject(op, "closed");
        }
        if (state->recv_op) {
            JsPromiseOp* op = state->recv_op;
            state->recv_op = NULL;
            js_promise_reject(op, "closed");
        }
        state->detach();
    }
    return JS_UNDEFINED;
}

static JSValue js_ws_new_client_object(JSContext* js, const std::shared_ptr<JsWsState>& state) {
    JSValue obj = JS_NewObjectClass(js, s_ws_class_id);
    if (JS_IsException(obj)) return obj;
    JsWsClient* box = new JsWsClient();
    box->state = state;
    state->js_alive = true;
    JS_SetOpaque(obj, box);
    JS_SetPropertyStr(js, obj, "send", JS_NewCFunction(js, js_ws_send, "send", 1));
    JS_SetPropertyStr(js, obj, "recv", JS_NewCFunction(js, js_ws_recv, "recv", 0));
    JS_SetPropertyStr(js, obj, "close", JS_NewCFunction(js, js_ws_close, "close", 0));
    return obj;
}

static JSValue js_ws_connect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->loop_ptr) {
        return js_rejected_promise(js, "hv.ws: no shared event loop on this thread");
    }
    if (argc < 1) {
        return js_rejected_promise(js, "hv.ws: connect needs url");
    }
    std::string url = js_to_string(js, argv[0]);
    js_ws_register_class(js);
    std::shared_ptr<JsWsState> state = std::make_shared<JsWsState>();
    state->client = std::make_shared<WebSocketClient>(task->loop_ptr);

    JsWsConnect* op = NULL;
    JSValue promise = js_new_promise<JsWsConnect>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    state->connect_op = op;
    op->state = state;
    state->client->onopen = [state]() {
        state->connected = true;
        state->closed = false;
        if (state->connect_op) {
            JsWsConnect* op = static_cast<JsWsConnect*>(state->connect_op);
            std::shared_ptr<JsWsState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            state->connect_op = NULL;
            JSValue obj = js_ws_new_client_object(op->task->js, hold);
            if (JS_IsException(obj)) {
                js_promise_reject(op, "hv.ws: create client failed");
                js_ws_detach_after_callback(loop, hold);
            }
            else {
                js_promise_resolve(op, obj);
            }
        }
    };
    state->client->onmessage = [state](const std::string& msg) {
        state->inbox.push_back(msg);
        js_ws_try_deliver(state);
    };
    state->client->onclose = [state]() {
        state->connected = false;
        state->closed = true;
        if (state->connect_op) {
            JsWsConnect* op = static_cast<JsWsConnect*>(state->connect_op);
            std::shared_ptr<JsWsState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            state->connect_op = NULL;
            js_promise_reject(op, "closed");
            js_ws_detach_after_callback(loop, hold);
        }
        js_ws_try_deliver(state);
    };
    task->in_call = true;
    int ret = state->client->open(url.c_str());
    if (ret != 0) {
        state->connect_op = NULL;
        js_promise_reject(op, "hv.ws: open failed");
        state->detach();
    }
    task->in_call = false;
    js_finish_deferred_op(op);
    return promise;
}

static JSValue js_require_ws(JSContext* js) {
    JSValue ws = JS_NewObject(js);
    JS_SetPropertyStr(js, ws, "connect", JS_NewCFunction(js, js_ws_connect, "connect", 1));
    return ws;
}
#endif
#ifdef HVJS_WITH_MQTT
static JSClassID s_mqtt_class_id;
static std::once_flag s_mqtt_class_once;

struct JsMqttMessage {
    std::string topic;
    std::string payload;
    int qos;
};

struct JsMqttState {
    mqtt_client_t* client;
    std::deque<JsMqttMessage> inbox;
    JsPromiseOp* connect_op;
    JsPromiseOp* recv_op;
    bool js_alive;
    bool closed;
    bool reconnect;

    JsMqttState() : client(NULL), connect_op(NULL), recv_op(NULL), js_alive(false), closed(false), reconnect(false) {}

    void detach() {
        closed = true;
        if (client) {
            mqtt_client_set_callback(client, NULL);
            mqtt_client_set_userdata(client, NULL);
            mqtt_client_free(client);
            client = NULL;
        }
    }

    ~JsMqttState() { detach(); }
};

struct JsMqttClient {
    std::shared_ptr<JsMqttState> state;
};

struct JsMqttConnect : public JsPromiseOp {
    std::shared_ptr<JsMqttState> state;
};

struct JsMqttRecv : public JsPromiseOp {
    std::shared_ptr<JsMqttState> state;
};

struct JsMqttDetachEvent {
    std::shared_ptr<JsMqttState> state;
};

static JsMqttClient* js_mqtt_client(JSContext* js, JSValueConst this_val) {
    return (JsMqttClient*)JS_GetOpaque2(js, this_val, s_mqtt_class_id);
}

static JSValue js_mqtt_recv(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_mqtt_publish(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_mqtt_subscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_mqtt_unsubscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_mqtt_disconnect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
static void js_mqtt_detach_after_callback(const EventLoopPtr& loop, hloop_t* raw_loop, const std::shared_ptr<JsMqttState>& state);

static void js_mqtt_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    JsMqttClient* box = (JsMqttClient*)JS_GetOpaque(val, s_mqtt_class_id);
    if (box && box->state) {
        box->state->js_alive = false;
        if (box->state->connect_op == NULL && box->state->recv_op == NULL) {
            box->state->detach();
        }
    }
    delete box;
}

static void js_mqtt_register_class(JSContext* js) {
    std::call_once(s_mqtt_class_once, []() { js_new_class_id(&s_mqtt_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_mqtt_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.mqtt.client";
        def.finalizer = js_mqtt_finalizer;
        JS_NewClass(rt, s_mqtt_class_id, &def);
    }
}

static JSValue js_mqtt_new_client_object(JSContext* js, const std::shared_ptr<JsMqttState>& state) {
    JSValue obj = JS_NewObjectClass(js, s_mqtt_class_id);
    if (JS_IsException(obj)) return obj;
    JsMqttClient* box = new JsMqttClient();
    box->state = state;
    state->js_alive = true;
    JS_SetOpaque(obj, box);
    JS_SetPropertyStr(js, obj, "recv", JS_NewCFunction(js, js_mqtt_recv, "recv", 0));
    JS_SetPropertyStr(js, obj, "publish", JS_NewCFunction(js, js_mqtt_publish, "publish", 2));
    JS_SetPropertyStr(js, obj, "subscribe", JS_NewCFunction(js, js_mqtt_subscribe, "subscribe", 1));
    JS_SetPropertyStr(js, obj, "unsubscribe", JS_NewCFunction(js, js_mqtt_unsubscribe, "unsubscribe", 1));
    JS_SetPropertyStr(js, obj, "disconnect", JS_NewCFunction(js, js_mqtt_disconnect, "disconnect", 0));
    return obj;
}

static JSValue js_push_mqtt_message(JSContext* js, const JsMqttMessage& msg) {
    JSValue obj = JS_NewObject(js);
    JS_SetPropertyStr(js, obj, "topic", JS_NewStringLen(js, msg.topic.data(), msg.topic.size()));
    JS_SetPropertyStr(js, obj, "payload", JS_NewStringLen(js, msg.payload.data(), msg.payload.size()));
    JS_SetPropertyStr(js, obj, "qos", JS_NewInt32(js, msg.qos));
    return obj;
}

static const char* js_mqtt_closed_reason(const JsMqttState* state) {
    return state && state->reconnect ? "reconnecting" : "closed";
}

static void js_mqtt_try_deliver(JsMqttState* state) {
    if (!state || state->recv_op == NULL) return;
    JsMqttRecv* op = static_cast<JsMqttRecv*>(state->recv_op);
    std::shared_ptr<JsMqttState> hold = op->state;
    EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
    hloop_t* raw_loop = op->task ? op->task->loop : NULL;
    if (!state->inbox.empty()) {
        JsMqttMessage msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        state->recv_op = NULL;
        js_promise_resolve(op, js_push_mqtt_message(op->task->js, msg));
    }
    else if (state->closed) {
        state->recv_op = NULL;
        js_promise_reject(op, js_mqtt_closed_reason(state));
    }
    if (!hold->js_alive && hold->connect_op == NULL && hold->recv_op == NULL) {
        js_mqtt_detach_after_callback(loop, raw_loop, hold);
    }
}

static void js_mqtt_detach_event_cb(hevent_t* ev) {
    JsMqttDetachEvent* detach = (JsMqttDetachEvent*)hevent_userdata(ev);
    if (detach) {
        detach->state->detach();
        delete detach;
    }
}

static void js_mqtt_detach_after_callback(const EventLoopPtr& loop, hloop_t* raw_loop, const std::shared_ptr<JsMqttState>& state) {
    if (!state) return;
    state->reconnect = false;
    if (state->client) {
        mqtt_client_set_reconnect(state->client, NULL);
    }
    if (loop) {
        loop->queueInLoop([state]() { state->detach(); });
    }
    else if (raw_loop) {
        JsMqttDetachEvent* detach = new JsMqttDetachEvent();
        detach->state = state;
        hevent_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.cb = js_mqtt_detach_event_cb;
        ev.userdata = detach;
        hloop_post_event(raw_loop, &ev);
    }
    else {
        state->detach();
    }
}

static void js_mqtt_on_event(mqtt_client_t* client, int type) {
    JsMqttState* state = (JsMqttState*)mqtt_client_get_userdata(client);
    if (state == NULL) return;
    switch (type) {
    case MQTT_TYPE_CONNACK:
        state->closed = false;
        if (state->connect_op) {
            JsMqttConnect* op = static_cast<JsMqttConnect*>(state->connect_op);
            std::shared_ptr<JsMqttState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->connect_op = NULL;
            JSValue obj = js_mqtt_new_client_object(op->task->js, hold);
            if (JS_IsException(obj)) {
                js_promise_reject(op, "hv.mqtt: create client failed");
                js_mqtt_detach_after_callback(loop, raw_loop, hold);
            }
            else {
                js_promise_resolve(op, obj);
            }
        }
        break;
    case MQTT_TYPE_PUBLISH: {
        JsMqttMessage msg;
        if (client->message.topic && client->message.topic_len > 0) {
            msg.topic.assign(client->message.topic, client->message.topic_len);
        }
        if (client->message.payload && client->message.payload_len > 0) {
            msg.payload.assign(client->message.payload, client->message.payload_len);
        }
        msg.qos = client->message.qos;
        state->inbox.push_back(std::move(msg));
        js_mqtt_try_deliver(state);
        break;
    }
    case MQTT_TYPE_DISCONNECT:
        state->closed = true;
        if (state->connect_op) {
            JsMqttConnect* op = static_cast<JsMqttConnect*>(state->connect_op);
            std::shared_ptr<JsMqttState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->connect_op = NULL;
            js_promise_reject(op, "connect failed");
            js_mqtt_detach_after_callback(loop, raw_loop, hold);
        }
        if (state->recv_op) {
            JsMqttRecv* op = static_cast<JsMqttRecv*>(state->recv_op);
            std::shared_ptr<JsMqttState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->recv_op = NULL;
            js_promise_reject(op, js_mqtt_closed_reason(state));
            if (!hold->js_alive && hold->connect_op == NULL && hold->recv_op == NULL) {
                js_mqtt_detach_after_callback(loop, raw_loop, hold);
            }
        }
        break;
    default: break;
    }
}

static bool js_parse_reconnect(JSContext* js, JSValueConst obj, reconn_setting_t* out) {
    JSValue reconnect;
    if (!js_get_property(js, obj, "reconnect", &reconnect) || !JS_IsObject(reconnect)) {
        if (!JS_IsUndefined(reconnect) && !JS_IsException(reconnect)) JS_FreeValue(js, reconnect);
        return false;
    }
    reconn_setting_init(out);
    out->min_delay = (uint32_t)js_get_int_property(js, reconnect, "min_delay", (int)out->min_delay);
    out->max_delay = (uint32_t)js_get_int_property(js, reconnect, "max_delay", (int)out->max_delay);
    out->delay_policy = (uint32_t)js_get_int_property(js, reconnect, "delay_policy", (int)out->delay_policy);
    out->max_retry_cnt = (uint32_t)js_get_int_property(js, reconnect, "max_retry", (int)out->max_retry_cnt);
    if (out->max_retry_cnt == 0) out->max_retry_cnt = INFINITE;
    if (out->min_delay == 0) out->min_delay = 1;
    if (out->max_delay < out->min_delay) out->max_delay = out->min_delay;
    if (out->delay_policy > 1 && out->delay_policy > UINT32_MAX / out->min_delay) {
        out->delay_policy = DEFAULT_RECONNECT_DELAY_POLICY;
    }
    JS_FreeValue(js, reconnect);
    return true;
}

static JSValue js_mqtt_connect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || task->loop == NULL) {
        return js_rejected_promise(js, "hv.mqtt: no event loop on this thread");
    }
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return js_rejected_promise(js, "hv.mqtt: connect needs options");
    }

    std::string host = js_get_string_property(js, argv[0], "host", "127.0.0.1");
    int port = js_get_int_property(js, argv[0], "port", DEFAULT_MQTT_PORT);
    int ssl = js_get_bool_property(js, argv[0], "ssl", false) ? 1 : 0;
    std::string id = js_get_string_property(js, argv[0], "id", "");
    std::string username = js_get_string_property(js, argv[0], "username", "");
    std::string password = js_get_string_property(js, argv[0], "password", "");
    int keepalive = js_get_int_property(js, argv[0], "keepalive", 0);
    int timeout = js_get_int_property(js, argv[0], "connect_timeout", 0);
    if (timeout <= 0) timeout = js_get_int_property(js, argv[0], "timeout", 0);
    bool clean_session = js_get_bool_property(js, argv[0], "clean_session", true);

    js_mqtt_register_class(js);
    std::shared_ptr<JsMqttState> state = std::make_shared<JsMqttState>();
    state->client = mqtt_client_new(task->loop);
    if (state->client == NULL) {
        return js_rejected_promise(js, "hv.mqtt: create client failed");
    }
    mqtt_client_set_userdata(state->client, state.get());
    mqtt_client_set_callback(state->client, js_mqtt_on_event);
    if (!id.empty()) mqtt_client_set_id(state->client, id.c_str());
    if (!username.empty() || !password.empty()) {
        mqtt_client_set_auth(state->client, username.c_str(), password.c_str());
    }
    if (keepalive > 0) state->client->keepalive = (unsigned short)keepalive;
    state->client->clean_session = clean_session ? 1 : 0;
    if (timeout > 0) mqtt_client_set_connect_timeout(state->client, timeout);
    reconn_setting_t reconn;
    if (js_parse_reconnect(js, argv[0], &reconn)) {
        mqtt_client_set_reconnect(state->client, &reconn);
        state->reconnect = true;
    }

    JsMqttConnect* op = NULL;
    JSValue promise = js_new_promise<JsMqttConnect>(js, task, &op);
    if (JS_IsException(promise)) {
        state->detach();
        return promise;
    }
    state->connect_op = op;
    op->state = state;
    task->in_call = true;
    int ret = mqtt_client_connect(state->client, host.c_str(), port, ssl);
    if (ret != 0) {
        state->connect_op = NULL;
        js_promise_reject(op, "hv.mqtt: connect failed");
        state->detach();
    }
    task->in_call = false;
    js_finish_deferred_op(op);
    return promise;
}

static JSValue js_mqtt_recv(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    JsMqttClient* box = js_mqtt_client(js, this_val);
    std::shared_ptr<JsMqttState> state = box ? box->state : std::shared_ptr<JsMqttState>();
    if (!state || state->client == NULL) {
        return js_rejected_promise(js, "closed");
    }
    if (!state->inbox.empty()) {
        JsMqttMessage msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        return js_async_resolved_promise(js, js_get_task(js), js_push_mqtt_message(js, msg));
    }
    if (state->closed) {
        return js_rejected_promise(js, js_mqtt_closed_reason(state.get()));
    }
    if (state->recv_op != NULL) {
        return js_rejected_promise(js, "hv.mqtt: recv already pending");
    }
    JsHttpTask* task = js_get_task(js);
    JsMqttRecv* op = NULL;
    JSValue promise = js_new_promise<JsMqttRecv>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->state = state;
    state->recv_op = op;
    return promise;
}

static JSValue js_mqtt_publish(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsMqttClient* box = js_mqtt_client(js, this_val);
    JsMqttState* state = box ? box->state.get() : NULL;
    if (state == NULL || state->client == NULL || state->closed) {
        return JS_ThrowTypeError(js, "hv.mqtt: closed");
    }
    if (argc < 2) {
        return JS_ThrowTypeError(js, "hv.mqtt: publish needs topic and payload");
    }
    std::string topic = js_to_string(js, argv[0]);
    std::string payload = js_to_string(js, argv[1]);
    int32_t qos = 0;
    if (argc > 2 && JS_ToInt32(js, &qos, argv[2]) != 0) return JS_EXCEPTION;
    int retain = argc > 3 ? JS_ToBool(js, argv[3]) : 0;
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.topic = topic.c_str();
    msg.topic_len = (unsigned int)topic.size();
    msg.payload = payload.c_str();
    msg.payload_len = (unsigned int)payload.size();
    msg.qos = (unsigned char)qos;
    msg.retain = (unsigned char)retain;
    int mid = mqtt_client_publish(state->client, &msg);
    if (mid < 0) {
        return JS_ThrowInternalError(js, "hv.mqtt: publish failed");
    }
    return JS_NewInt32(js, mid);
}

static JSValue js_mqtt_subscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsMqttClient* box = js_mqtt_client(js, this_val);
    JsMqttState* state = box ? box->state.get() : NULL;
    if (state == NULL || state->client == NULL || state->closed) {
        return JS_ThrowTypeError(js, "hv.mqtt: closed");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(js, "hv.mqtt: subscribe needs topic");
    }
    std::string topic = js_to_string(js, argv[0]);
    int32_t qos = 0;
    if (argc > 1 && JS_ToInt32(js, &qos, argv[1]) != 0) return JS_EXCEPTION;
    int mid = mqtt_client_subscribe(state->client, topic.c_str(), qos);
    if (mid < 0) {
        return JS_ThrowInternalError(js, "hv.mqtt: subscribe failed");
    }
    return JS_NewInt32(js, mid);
}

static JSValue js_mqtt_unsubscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsMqttClient* box = js_mqtt_client(js, this_val);
    JsMqttState* state = box ? box->state.get() : NULL;
    if (state == NULL || state->client == NULL || state->closed) {
        return JS_ThrowTypeError(js, "hv.mqtt: closed");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(js, "hv.mqtt: unsubscribe needs topic");
    }
    std::string topic = js_to_string(js, argv[0]);
    int mid = mqtt_client_unsubscribe(state->client, topic.c_str());
    if (mid < 0) {
        return JS_ThrowInternalError(js, "hv.mqtt: unsubscribe failed");
    }
    return JS_NewInt32(js, mid);
}

static JSValue js_mqtt_disconnect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    JsMqttClient* box = js_mqtt_client(js, this_val);
    if (box && box->state) {
        std::shared_ptr<JsMqttState> state = box->state;
        state->reconnect = false;
        if (state->connect_op) {
            JsPromiseOp* op = state->connect_op;
            state->connect_op = NULL;
            js_promise_reject(op, "closed");
        }
        if (state->recv_op) {
            JsPromiseOp* op = state->recv_op;
            state->recv_op = NULL;
            js_promise_reject(op, "closed");
        }
        state->detach();
    }
    return JS_UNDEFINED;
}

static JSValue js_require_mqtt(JSContext* js) {
    JSValue mqtt = JS_NewObject(js);
    JS_SetPropertyStr(js, mqtt, "connect", JS_NewCFunction(js, js_mqtt_connect, "connect", 1));
    return mqtt;
}
#endif

static JSValue js_require(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(js, "require needs a module name");
    }
    std::string name = js_to_string(js, argv[0]);
    if (name == "hv") {
        JSValue hv = JS_NewObject(js);
        JS_SetPropertyStr(js, hv, "version", JS_NewCFunction(js, js_hv_version, "version", 0));
        JS_SetPropertyStr(js, hv, "log", JS_NewCFunction(js, js_hv_log, "log", 1));
        JS_SetPropertyStr(js, hv, "sleep", JS_NewCFunction(js, js_hv_sleep, "sleep", 1));
        return hv;
    }
#ifdef HVJS_WITH_HTTP
    if (name == "hv/http") {
        return js_require_http(js);
    }
#endif
#ifdef HVJS_WITH_REDIS
    if (name == "hv/redis") {
        return js_require_redis(js);
    }
#endif
#ifdef HVJS_WITH_HTTP
    if (name == "hv/ws") {
        return js_require_ws(js);
    }
#endif
#ifdef HVJS_WITH_MQTT
    if (name == "hv/mqtt") {
        return js_require_mqtt(js);
    }
#endif
    return JS_ThrowReferenceError(js, "module '%s' is not available", name.c_str());
}

static bool load_file(const std::string& filepath, std::string* out, std::string* err) {
    HFile file;
    if (file.open(filepath.c_str(), "rb") != 0) {
        if (err) *err = strerror(errno);
        return false;
    }
    size_t size = hv_filesize(filepath.c_str());
    out->resize(size);
    if (size > 0) {
        int nread = file.read(&(*out)[0], (int)size);
        if (nread < 0 || (size_t)nread != size) {
            if (err) *err = "read script failed";
            return false;
        }
    }
    return true;
}

static time_t file_mtime(const std::string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mtime;
}

static bool push_handler_fn(JSContext* js, JSValueConst global, http_method method, JSValue* fn) {
    std::string name = http_method_str(method);
    tolower(name);
    *fn = JS_GetPropertyStr(js, global, name.c_str());
    if (JS_IsFunction(js, *fn)) return true;
    JS_FreeValue(js, *fn);
    *fn = JS_GetPropertyStr(js, global, "handle");
    if (JS_IsFunction(js, *fn)) return true;
    JS_FreeValue(js, *fn);
    *fn = JS_UNDEFINED;
    return false;
}

static bool apply_result(JSContext* js, JSValueConst value, const HttpContextPtr& ctx, std::string* err) {
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        return true;
    }
    if (JS_IsNumber(value)) {
        int32_t status = 0;
        if (JS_ToInt32(js, &status, value) == 0 && ctx->response->status_code == HTTP_STATUS_OK) {
            ctx->response->status_code = (http_status)status;
        }
        return true;
    }
    if (JS_IsString(value)) {
        std::string body = js_to_string(js, value);
        ctx->response->String(body);
        return true;
    }
    JSValue json = JS_JSONStringify(js, value, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(json)) {
        std::string body = js_to_string(js, json);
        ctx->response->SetContentType(APPLICATION_JSON);
        ctx->response->body = body;
        JS_FreeValue(js, json);
        return true;
    }
    if (err) *err = js_exception_string(js);
    return false;
}

static void task_finish(JsHttpTask* task, JSValue result) {
    if (task->finished) return;
    task->finished = true;
    if (!task->error.empty()) {
        hloge("[js] http handler error: %s", task->error.c_str());
        task->ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        task->ctx->response->String(task->error);
    }
    else if (!JS_IsUndefined(task->promise) && JS_PromiseState(task->js, task->promise) == JS_PROMISE_REJECTED) {
        std::string err = js_to_string(task->js, result);
        hloge("[js] http handler rejected: %s", err.c_str());
        task->ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        task->ctx->response->String(err);
    }
    else {
        std::string err;
        if (!apply_result(task->js, result, task->ctx, &err)) {
            hloge("[js] http handler error: %s", err.c_str());
            task->ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            task->ctx->response->String(err);
        }
    }
    JS_FreeValue(task->js, result);
    if (task->async) {
        task->ctx->send();
    }
    task_unref(task);
}

} // namespace

struct HttpJsHandler::State {
    std::mutex mutex;
    std::string code;
    time_t mtime;
    bool loaded;

    State() : mtime(0), loaded(false) {}
};

HttpJsHandler::HttpJsHandler(const char* filepath, const HttpJsHandlerOptions& options)
    : filepath_(filepath ? filepath : ""), options_(options), state_(std::make_shared<State>()) {}

bool HttpJsHandler::loadScript(std::string* code, std::string* err) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->loaded && !options_.reload_on_change) {
        if (code) *code = state_->code;
        return true;
    }

    time_t mtime = file_mtime(filepath_);
    if (mtime == 0) {
        if (err) *err = strerror(errno);
        return false;
    }
    if (state_->loaded && state_->mtime == mtime) {
        if (code) *code = state_->code;
        return true;
    }

    std::string latest;
    if (!load_file(filepath_, &latest, err)) {
        return false;
    }
    state_->code = latest;
    state_->mtime = mtime;
    state_->loaded = true;
    if (code) *code = state_->code;
    return true;
}

int HttpJsHandler::operator()(const HttpContextPtr& ctx) {
    if (!ctx || !ctx->request || !ctx->response) {
        if (ctx && ctx->response) {
            ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            ctx->response->String("js handler: invalid http context");
        }
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    std::string code, err;
    if (!loadScript(&code, &err)) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(err);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    JsHttpTask* task = new JsHttpTask();
    task->ctx = ctx;
    task->rt = JS_NewRuntime();
    task->js = task->rt ? JS_NewContext(task->rt) : NULL;
    if (task->rt == NULL || task->js == NULL) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("js handler: failed to create quickjs runtime");
        task_unref(task);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
    JS_SetContextOpaque(task->js, task);
    if (ctx->writer && ctx->writer->io()) {
        task->loop = hevent_loop(ctx->writer->io());
    }
    task->loop_ptr = currentThreadEventLoopPtr;
    if (task->loop == NULL && task->loop_ptr) {
        task->loop = task->loop_ptr->loop();
    }
    if (task->loop == NULL) {
        EventLoop* loop = currentThreadEventLoop;
        if (loop) {
            task->loop = loop->loop();
        }
    }
    if (task->loop == NULL) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("js handler: no event loop on this thread");
        task_unref(task);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    JSValue global = JS_GetGlobalObject(task->js);
    JS_SetPropertyStr(task->js, global, "require", JS_NewCFunction(task->js, js_require, "require", 1));

    JSValue eval = JS_Eval(task->js, code.c_str(), code.size(), filepath_.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(eval)) {
        std::string msg = js_exception_string(task->js);
        JS_FreeValue(task->js, global);
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(msg);
        task_unref(task);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
    JS_FreeValue(task->js, eval);

    JSValue fn;
    if (!push_handler_fn(task->js, global, ctx->request->method, &fn)) {
        JS_FreeValue(task->js, global);
        ctx->response->status_code = HTTP_STATUS_NOT_IMPLEMENTED;
        ctx->response->String("no js handler function");
        task_unref(task);
        return HTTP_STATUS_NOT_IMPLEMENTED;
    }

    JSValue js_ctx = js_new_ctx(task->js, ctx);
    JSValue ret = JS_Call(task->js, fn, JS_UNDEFINED, 1, &js_ctx);
    JS_FreeValue(task->js, js_ctx);
    JS_FreeValue(task->js, fn);
    if (JS_IsException(ret)) {
        std::string msg = js_exception_string(task->js);
        JS_FreeValue(task->js, global);
        JS_FreeValue(task->js, ret);
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(msg);
        task_unref(task);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    JSValue promise_ctor = JS_GetPropertyStr(task->js, global, "Promise");
    JSValue promise_resolve = JS_GetPropertyStr(task->js, promise_ctor, "resolve");
    JS_FreeValue(task->js, global);
    JSValue promise_arg = ret;
    task->promise = JS_Call(task->js, promise_resolve, promise_ctor, 1, &promise_arg);
    JS_FreeValue(task->js, promise_resolve);
    JS_FreeValue(task->js, promise_ctor);
    JS_FreeValue(task->js, ret);
    if (JS_IsException(task->promise)) {
        std::string msg = js_exception_string(task->js);
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(msg);
        task_unref(task);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    task_ref(task);
    drain_jobs(task);
    bool finished = task->finished;
    int status = ctx->response->status_code;
    if (!finished) {
        task->async = true;
    }
    task_unref(task);
    if (finished) {
        return status;
    }
    return HTTP_STATUS_NEXT;
}

} // namespace hv
#endif // WITH_JS
