#ifdef WITH_JS

#include "hvjs.h"

#ifdef HVJS_WITH_HTTP

#include <stdint.h>
#include <string.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "AsyncHttpClient.h"
#include "WebSocketClient.h"
#include "hstring.h"

namespace hv {
namespace js {
namespace {

static const int JS_HTTP_METHOD_REQUEST = -1;

void js_http_release_client_after_callback(const EventLoopPtr& loop, const std::shared_ptr<AsyncHttpClient>& client) {
    if (!client) return;
    if (loop && loop->loop() && hloop_status(loop->loop()) == HLOOP_STATUS_RUNNING) {
        loop->queueInLoop([client]() {});
    }
}

struct HvJsHttpRequest : public HvJsPromiseOp {
    HttpRequestPtr req;
    std::shared_ptr<AsyncHttpClient> client;

    void cancel(const char* reason) override {
        if (req) {
            req->Cancel();
        }
        std::shared_ptr<AsyncHttpClient> hold = client;
        client.reset();
        js_http_release_client_after_callback(task ? task->loop_ptr : EventLoopPtr(), hold);
        HvJsPromiseOp::cancel(reason);
    }
};

JSValue js_push_headers(JSContext* js, const http_headers& headers) {
    JSValue obj = JS_NewObject(js);
    for (auto& kv : headers) {
        JS_SetPropertyStr(js, obj, kv.first.c_str(), JS_NewStringLen(js, kv.second.data(), kv.second.size()));
    }
    return obj;
}

JSValue js_push_http_response(JSContext* js, const HttpResponsePtr& resp) {
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

int js_fill_http_request(JSContext* js, JSValueConst* argv, int argc, http_method method, int url_index, HttpRequestPtr* out) {
    if (argc <= url_index) {
        return -1;
    }
    std::string url = hvjs_to_string(js, argv[url_index]);
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    if (argc > url_index + 1 && !JS_IsUndefined(argv[url_index + 1]) && !JS_IsNull(argv[url_index + 1])) {
        std::string body = hvjs_to_string(js, argv[url_index + 1]);
        req->body = body;
    }
    if (argc > url_index + 2 && JS_IsObject(argv[url_index + 2])) {
        JSPropertyEnum* tab = NULL;
        uint32_t len = 0;
        if (JS_GetOwnPropertyNames(js, &tab, &len, argv[url_index + 2], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < len; ++i) {
                JSValue key = JS_AtomToString(js, tab[i].atom);
                JSValue value = JS_GetProperty(js, argv[url_index + 2], tab[i].atom);
                std::string k = hvjs_to_string(js, key);
                std::string v = hvjs_to_string(js, value);
                if (!k.empty()) req->headers[k] = v;
                JS_FreeValue(js, value);
                JS_FreeValue(js, key);
            }
            js_free(js, tab);
        }
    }
    *out = req;
    return 0;
}

JSValue js_http_request(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    (void)this_val;
    HvJsTask* task = hvjs_get_task(js);
    if (task == NULL || !task->loop_ptr) {
        return hvjs_rejected_promise(js, "hv.http: no shared event loop on this thread");
    }
    http_method method = (http_method)magic;
    int url_index = 0;
    if (magic == JS_HTTP_METHOD_REQUEST) {
        if (argc < 2) return hvjs_rejected_promise(js, "hv.http: request needs method and url");
        std::string m = hvjs_to_string(js, argv[0]);
        toupper(m);
        method = http_method_enum(m.c_str());
        url_index = 1;
    }
    if (method == HTTP_CUSTOM_METHOD) {
        return hvjs_rejected_promise(js, "hv.http: unsupported method");
    }

    HttpRequestPtr req;
    if (js_fill_http_request(js, argv, argc, method, url_index, &req) != 0) {
        return hvjs_rejected_promise(js, "hv.http: missing url");
    }

    HvJsHttpRequest* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsHttpRequest>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->req = req;
    op->client = std::make_shared<AsyncHttpClient>(task->loop_ptr);
    std::shared_ptr<AsyncHttpClient> client = op->client;
    std::shared_ptr<HvJsPromiseOp*> handle = op->handle;
    ++task->in_call;
    int ret = client->send(req, [handle, client](const HttpResponsePtr& resp) {
        HvJsPromiseOp* base = handle ? *handle : NULL;
        if (base == NULL || base->task == NULL) return;
        HvJsHttpRequest* op = static_cast<HvJsHttpRequest*>(base);
        op->client.reset();
        js_http_release_client_after_callback(base->task->loop_ptr, client);
        JSContext* js = base->task->js;
        if (resp) {
            hvjs_promise_resolve(op, js_push_http_response(js, resp));
        }
        else {
            hvjs_promise_reject(op, "hv.http: request failed");
        }
    });
    if (ret != 0) {
        hvjs_promise_reject(op, "hv.http: request failed");
    }
    --task->in_call;
    hvjs_finish_deferred_op(op);
    return promise;
}

static JSClassID s_ws_class_id;
static std::once_flag s_ws_class_once;

struct HvJsWsState {
    std::shared_ptr<WebSocketClient> client;
    std::deque<std::string> inbox;
    HvJsPromiseOp* connect_op;
    HvJsPromiseOp* recv_op;
    bool js_alive;
    bool connected;
    bool closed;

    HvJsWsState() : connect_op(NULL), recv_op(NULL), js_alive(false), connected(false), closed(false) {}

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

    ~HvJsWsState() { detach(); }
};

void js_ws_detach_after_callback(const EventLoopPtr& loop, hloop_t* raw_loop, const std::shared_ptr<HvJsWsState>& state);

struct HvJsWsClient {
    std::shared_ptr<HvJsWsState> state;
};

struct HvJsWsDetachEvent {
    std::shared_ptr<HvJsWsState> state;
};

struct HvJsWsConnect : public HvJsPromiseOp {
    std::shared_ptr<HvJsWsState> state;

    void cancel(const char* reason) override {
        std::shared_ptr<HvJsWsState> hold = state;
        if (hold) {
            hold->connect_op = NULL;
            js_ws_detach_after_callback(task ? task->loop_ptr : EventLoopPtr(), task ? task->loop : NULL, hold);
        }
        HvJsPromiseOp::cancel(reason);
    }
};

struct HvJsWsRecv : public HvJsPromiseOp {
    std::shared_ptr<HvJsWsState> state;

    void cancel(const char* reason) override {
        std::shared_ptr<HvJsWsState> hold = state;
        if (hold) {
            hold->recv_op = NULL;
            if (!hold->js_alive && hold->connect_op == NULL) {
                js_ws_detach_after_callback(task ? task->loop_ptr : EventLoopPtr(), task ? task->loop : NULL, hold);
            }
        }
        HvJsPromiseOp::cancel(reason);
    }
};

HvJsWsClient* js_ws_client(JSContext* js, JSValueConst this_val) {
    return (HvJsWsClient*)JS_GetOpaque2(js, this_val, s_ws_class_id);
}

void js_ws_detach_event_cb(hevent_t* ev) {
    HvJsWsDetachEvent* detach = (HvJsWsDetachEvent*)hevent_userdata(ev);
    if (detach) {
        detach->state->detach();
        delete detach;
    }
}

void js_ws_detach_after_callback(const EventLoopPtr& loop, hloop_t* raw_loop, const std::shared_ptr<HvJsWsState>& state) {
    if (!state) return;
    hloop_t* event_loop = loop ? loop->loop() : NULL;
    if (loop && event_loop && hloop_status(event_loop) == HLOOP_STATUS_RUNNING) {
        loop->queueInLoop([state]() { state->detach(); });
    }
    else if (raw_loop && hloop_status(raw_loop) == HLOOP_STATUS_RUNNING) {
        HvJsWsDetachEvent* detach = new HvJsWsDetachEvent();
        detach->state = state;
        hevent_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.cb = js_ws_detach_event_cb;
        ev.userdata = detach;
        hloop_post_event(raw_loop, &ev);
    }
    else {
        state->detach();
    }
}

void js_ws_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    HvJsWsClient* box = (HvJsWsClient*)JS_GetOpaque(val, s_ws_class_id);
    if (box && box->state) {
        box->state->js_alive = false;
        if (box->state->connect_op == NULL && box->state->recv_op == NULL) {
            box->state->detach();
        }
    }
    delete box;
}

void js_ws_register_class(JSContext* js) {
    std::call_once(s_ws_class_once, []() { hvjs_new_class_id(&s_ws_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_ws_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.ws.client";
        def.finalizer = js_ws_finalizer;
        JS_NewClass(rt, s_ws_class_id, &def);
    }
}

void js_ws_try_deliver(const std::shared_ptr<HvJsWsState>& state) {
    if (!state || state->recv_op == NULL) return;
    HvJsWsRecv* op = static_cast<HvJsWsRecv*>(state->recv_op);
    std::shared_ptr<HvJsWsState> hold = op->state;
    EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
    hloop_t* raw_loop = op->task ? op->task->loop : NULL;
    if (!state->inbox.empty()) {
        std::string msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        state->recv_op = NULL;
        hvjs_promise_resolve(op, JS_NewStringLen(op->task->js, msg.data(), msg.size()));
    }
    else if (state->closed) {
        state->recv_op = NULL;
        hvjs_promise_reject(op, "closed");
    }
    if (!hold->js_alive && hold->connect_op == NULL && hold->recv_op == NULL) {
        js_ws_detach_after_callback(loop, raw_loop, hold);
    }
}

JSValue js_ws_send(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    HvJsWsClient* box = js_ws_client(js, this_val);
    HvJsWsState* state = box ? box->state.get() : NULL;
    if (state == NULL || !state->client || !state->connected) {
        return JS_ThrowTypeError(js, "hv.ws: closed");
    }
    std::string msg = argc > 0 ? hvjs_to_string(js, argv[0]) : std::string();
    enum ws_opcode opcode = WS_OPCODE_TEXT;
    if (argc > 1 && hvjs_to_string(js, argv[1]) == "binary") {
        opcode = WS_OPCODE_BINARY;
    }
    int ret = state->client->send(msg.data(), (int)msg.size(), opcode);
    if (ret < 0) {
        return JS_ThrowInternalError(js, "hv.ws: send failed");
    }
    return JS_NewInt32(js, ret);
}

JSValue js_ws_recv(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    HvJsWsClient* box = js_ws_client(js, this_val);
    HvJsWsState* state = box ? box->state.get() : NULL;
    if (state == NULL || !state->client) {
        return hvjs_rejected_promise(js, "closed");
    }
    if (!state->inbox.empty()) {
        std::string msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        return hvjs_async_resolved_promise(js, hvjs_get_task(js), JS_NewStringLen(js, msg.data(), msg.size()));
    }
    if (state->closed || !state->connected) {
        return hvjs_rejected_promise(js, "closed");
    }
    if (state->recv_op != NULL) {
        return hvjs_rejected_promise(js, "hv.ws: recv already pending");
    }
    HvJsTask* task = hvjs_get_task(js);
    HvJsWsRecv* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsWsRecv>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->state = box->state;
    state->recv_op = op;
    return promise;
}

JSValue js_ws_close(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    HvJsWsClient* box = js_ws_client(js, this_val);
    if (box && box->state) {
        std::shared_ptr<HvJsWsState> state = box->state;
        if (state->connect_op) {
            HvJsPromiseOp* op = state->connect_op;
            state->connect_op = NULL;
            hvjs_promise_reject(op, "closed");
        }
        if (state->recv_op) {
            HvJsPromiseOp* op = state->recv_op;
            state->recv_op = NULL;
            hvjs_promise_reject(op, "closed");
        }
        state->detach();
    }
    return JS_UNDEFINED;
}

JSValue js_ws_new_client_object(JSContext* js, const std::shared_ptr<HvJsWsState>& state) {
    JSValue obj = JS_NewObjectClass(js, s_ws_class_id);
    if (JS_IsException(obj)) return obj;
    HvJsWsClient* box = new HvJsWsClient();
    box->state = state;
    state->js_alive = true;
    JS_SetOpaque(obj, box);
    JS_SetPropertyStr(js, obj, "send", JS_NewCFunction(js, js_ws_send, "send", 1));
    JS_SetPropertyStr(js, obj, "recv", JS_NewCFunction(js, js_ws_recv, "recv", 0));
    JS_SetPropertyStr(js, obj, "close", JS_NewCFunction(js, js_ws_close, "close", 0));
    return obj;
}

JSValue js_ws_connect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    HvJsTask* task = hvjs_get_task(js);
    if (task == NULL || !task->loop_ptr) {
        return hvjs_rejected_promise(js, "hv.ws: no shared event loop on this thread");
    }
    if (argc < 1) {
        return hvjs_rejected_promise(js, "hv.ws: connect needs url");
    }
    std::string url = hvjs_to_string(js, argv[0]);
    js_ws_register_class(js);
    std::shared_ptr<HvJsWsState> state = std::make_shared<HvJsWsState>();
    state->client = std::make_shared<WebSocketClient>(task->loop_ptr);
    if (argc > 1 && JS_IsObject(argv[1])) {
        int timeout = hvjs_get_int_property(js, argv[1], "connect_timeout", 0);
        if (timeout <= 0) timeout = hvjs_get_int_property(js, argv[1], "timeout", 0);
        int ping_interval = hvjs_get_int_property(js, argv[1], "ping_interval", 0);
        if (timeout > 0) state->client->setConnectTimeout(timeout);
        if (ping_interval > 0) state->client->setPingInterval(ping_interval);
    }

    HvJsWsConnect* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsWsConnect>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    state->connect_op = op;
    op->state = state;
    state->client->onopen = [state]() {
        state->connected = true;
        state->closed = false;
        if (state->connect_op) {
            HvJsWsConnect* op = static_cast<HvJsWsConnect*>(state->connect_op);
            std::shared_ptr<HvJsWsState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->connect_op = NULL;
            JSValue obj = js_ws_new_client_object(op->task->js, hold);
            if (JS_IsException(obj)) {
                hvjs_promise_reject(op, "hv.ws: create client failed");
                js_ws_detach_after_callback(loop, raw_loop, hold);
            }
            else {
                hvjs_promise_resolve(op, obj);
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
            HvJsWsConnect* op = static_cast<HvJsWsConnect*>(state->connect_op);
            std::shared_ptr<HvJsWsState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->connect_op = NULL;
            hvjs_promise_reject(op, "closed");
            js_ws_detach_after_callback(loop, raw_loop, hold);
        }
        js_ws_try_deliver(state);
    };
    ++task->in_call;
    int ret = state->client->open(url.c_str());
    if (ret != 0) {
        state->connect_op = NULL;
        hvjs_promise_reject(op, "hv.ws: open failed");
        state->detach();
    }
    --task->in_call;
    hvjs_finish_deferred_op(op);
    return promise;
}

} // namespace

JSValue hvjs_require_http(JSContext* js) {
    JSValue http = JS_NewObject(js);
    JS_SetPropertyStr(js, http, "request", JS_NewCFunctionMagic(js, js_http_request, "request", 2, JS_CFUNC_generic_magic, JS_HTTP_METHOD_REQUEST));
    JS_SetPropertyStr(js, http, "get", JS_NewCFunctionMagic(js, js_http_request, "get", 1, JS_CFUNC_generic_magic, HTTP_GET));
    JS_SetPropertyStr(js, http, "post", JS_NewCFunctionMagic(js, js_http_request, "post", 2, JS_CFUNC_generic_magic, HTTP_POST));
    JS_SetPropertyStr(js, http, "put", JS_NewCFunctionMagic(js, js_http_request, "put", 2, JS_CFUNC_generic_magic, HTTP_PUT));
    JS_SetPropertyStr(js, http, "delete", JS_NewCFunctionMagic(js, js_http_request, "delete", 1, JS_CFUNC_generic_magic, HTTP_DELETE));
    return http;
}

JSValue hvjs_require_ws(JSContext* js) {
    JSValue ws = JS_NewObject(js);
    JS_SetPropertyStr(js, ws, "connect", JS_NewCFunction(js, js_ws_connect, "connect", 1));
    return ws;
}

} // namespace js
} // namespace hv

#endif // HVJS_WITH_HTTP
#endif // WITH_JS
