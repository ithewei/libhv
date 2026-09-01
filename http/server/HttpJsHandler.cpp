#ifdef WITH_JS

#include "HttpJsHandler.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <memory>
#include <mutex>
#include <string>

#include "EventLoop.h"
#include "hfile.h"
#include "hlog.h"
#include "hstring.h"
#include "hvjs.h"

namespace hv {

namespace {

struct JsHttpTask : public hv::js::HvJsTask {
    HttpContextPtr ctx;
    bool async;

    JsHttpTask() : async(false) {}
};

static JsHttpTask* js_get_task(JSContext* js) {
    return static_cast<JsHttpTask*>(hv::js::hvjs_get_task(js));
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
    std::string key = argc > 0 ? hv::js::hvjs_to_string(js, argv[0]) : std::string();
    std::string defvalue = argc > 1 ? hv::js::hvjs_to_string(js, argv[1]) : std::string();
    std::string value = task->ctx->param(key.c_str(), defvalue);
    return JS_NewStringLen(js, value.data(), value.size());
}

static JSValue js_ctx_header(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string key = argc > 0 ? hv::js::hvjs_to_string(js, argv[0]) : std::string();
    std::string defvalue = argc > 1 ? hv::js::hvjs_to_string(js, argv[1]) : std::string();
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
    std::string key = hv::js::hvjs_to_string(js, argv[0]);
    std::string value = hv::js::hvjs_to_string(js, argv[1]);
    task->ctx->setHeader(key.c_str(), value);
    return JS_UNDEFINED;
}

static JSValue js_ctx_text(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    JsHttpTask* task = js_get_task(js);
    if (task == NULL || !task->ctx || argc < 1) {
        return JS_ThrowTypeError(js, "invalid HttpContext");
    }
    std::string text = hv::js::hvjs_to_string(js, argv[0]);
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
    std::string body = hv::js::hvjs_to_string(js, json);
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

static void http_js_task_finish(hv::js::HvJsTask* task, JSValue result) {
    task_finish(static_cast<JsHttpTask*>(task), result);
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
        std::string body = hv::js::hvjs_to_string(js, value);
        ctx->response->String(body);
        return true;
    }
    JSValue json = JS_JSONStringify(js, value, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(json)) {
        std::string body = hv::js::hvjs_to_string(js, json);
        ctx->response->SetContentType(APPLICATION_JSON);
        ctx->response->body = body;
        JS_FreeValue(js, json);
        return true;
    }
    if (err) *err = hv::js::hvjs_exception_string(js);
    return false;
}

static void task_finish(JsHttpTask* task, JSValue result) {
    if (task->finished) return;
    task->finished = true;
    if (!task->error.empty()) {
        hloge("[js] http handler error: %s", task->error.c_str());
        task->ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        task->ctx->response->String("javascript handler error");
    }
    else if (task->promise_rejected) {
        std::string err = hv::js::hvjs_to_string(task->js, result);
        hloge("[js] http handler rejected: %s", err.c_str());
        task->ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        task->ctx->response->String("javascript handler error");
    }
    else {
        std::string err;
        if (!apply_result(task->js, result, task->ctx, &err)) {
            hloge("[js] http handler error: %s", err.c_str());
            task->ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            task->ctx->response->String("javascript handler error");
        }
    }
    JS_FreeValue(task->js, result);
    if (task->async) {
        task->ctx->send();
    }
    // Cancel pending ops + timeout and release the task's reference.
    hv::js::hvjs_task_close(task, "javascript task finished");
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
        hloge("[js] load %s failed: %s", filepath_.c_str(), err.c_str());
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("javascript handler error");
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    hloop_t* loop = NULL;
    if (ctx->writer && ctx->writer->io()) {
        loop = hevent_loop(ctx->writer->io());
    }
    if (loop == NULL) {
        EventLoop* current = currentThreadEventLoop;
        if (current) {
            loop = current->loop();
        }
    }
    if (loop == NULL) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("js handler: no event loop on this thread");
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    JsHttpTask* task = new JsHttpTask();
    task->ctx = ctx;
    task->finish = http_js_task_finish;
    if (!hv::js::hvjs_runtime_add_task(hv::js::hvjs_runtime(loop), task, options_.timeout_ms)) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("js handler: failed to setup js task");
        hv::js::hvjs_task_close(task, "javascript handler error");
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    {
        hv::js::HvJsTaskScope scope(task);
        JSValue global = JS_GetGlobalObject(task->js);
        JS_SetPropertyStr(task->js, global, "require", JS_NewCFunction(task->js, hv::js::hvjs_require, "require", 1));

        JSValue eval = JS_Eval(task->js, code.c_str(), code.size(), filepath_.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(eval)) {
            std::string msg = hv::js::hvjs_exception_string(task->js);
            hloge("[js] eval %s failed: %s", filepath_.c_str(), msg.c_str());
            JS_FreeValue(task->js, global);
            ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            ctx->response->String("javascript handler error");
            hv::js::hvjs_task_close(task, "javascript handler error");
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        JS_FreeValue(task->js, eval);

        JSValue fn;
        if (!push_handler_fn(task->js, global, ctx->request->method, &fn)) {
            JS_FreeValue(task->js, global);
            ctx->response->status_code = HTTP_STATUS_NOT_IMPLEMENTED;
            ctx->response->String("no js handler function");
            hv::js::hvjs_task_close(task, "javascript handler error");
            return HTTP_STATUS_NOT_IMPLEMENTED;
        }

        JSValue js_ctx = js_new_ctx(task->js, ctx);
        JSValue ret = JS_Call(task->js, fn, JS_UNDEFINED, 1, &js_ctx);
        JS_FreeValue(task->js, js_ctx);
        JS_FreeValue(task->js, fn);
        if (JS_IsException(ret)) {
            std::string msg = hv::js::hvjs_exception_string(task->js);
            hloge("[js] handler %s failed: %s", filepath_.c_str(), msg.c_str());
            JS_FreeValue(task->js, global);
            JS_FreeValue(task->js, ret);
            ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            ctx->response->String("javascript handler error");
            hv::js::hvjs_task_close(task, "javascript handler error");
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        JS_FreeValue(task->js, global);

        if (!hv::js::hvjs_task_await(task, ret, &err)) {
            hloge("[js] await %s failed: %s", filepath_.c_str(), err.c_str());
            ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            ctx->response->String("javascript handler error");
            hv::js::hvjs_task_close(task, "javascript handler error");
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
    }

    if (hv::js::hvjs_task_poll(task) == 0) {
        task->async = true;
        return HTTP_STATUS_NEXT;
    }
    return ctx->response->status_code;
}

} // namespace hv
#endif // WITH_JS
