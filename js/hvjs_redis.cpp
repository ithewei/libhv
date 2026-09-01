#ifdef WITH_JS

#include "hvjs.h"

#ifdef HVJS_WITH_REDIS

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <mutex>
#include <string>

#include "AsyncRedisClient.h"

namespace hv {
namespace js {
namespace {

static JSClassID s_redis_class_id;
static std::once_flag s_redis_class_once;

struct HvJsRedisState {
    std::shared_ptr<AsyncRedisClient> client;
    bool destroyed;

    HvJsRedisState() : destroyed(false) {}

    ~HvJsRedisState() {
        destroyed = true;
        if (client) {
            client->stop(true);
            client.reset();
        }
    }
};

struct HvJsRedisClient {
    std::shared_ptr<HvJsRedisState> state;
};

struct HvJsRedisCommand : public HvJsPromiseOp {
    std::shared_ptr<HvJsRedisState> redis;
};

void js_redis_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    HvJsRedisClient* box = (HvJsRedisClient*)JS_GetOpaque(val, s_redis_class_id);
    if (box) {
        delete box;
    }
}

HvJsRedisClient* js_redis_client(JSContext* js, JSValueConst this_val) {
    HvJsRedisClient* box = (HvJsRedisClient*)JS_GetOpaque2(js, this_val, s_redis_class_id);
    return box;
}

void js_redis_register_class(JSContext* js) {
    std::call_once(s_redis_class_once, []() { hvjs_new_class_id(&s_redis_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_redis_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.redis.client";
        def.finalizer = js_redis_finalizer;
        JS_NewClass(rt, s_redis_class_id, &def);
    }
}

JSValue js_push_redis_reply(JSContext* js, const RedisReply& reply) {
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

void js_redis_resolve_result(HvJsRedisCommand* op, const RedisResult& result) {
    JSContext* js = op->task->js;
    if (!op->redis || op->redis->destroyed) {
        hvjs_promise_reject(op, "hv.redis: client closed");
        return;
    }
    if (result.code != 0) {
        char err[64];
        snprintf(err, sizeof(err), "hv.redis: request failed (%d)", result.code);
        hvjs_promise_reject(op, err);
        return;
    }
    if (result.reply.isError()) {
        hvjs_promise_reject(op, result.reply.error().c_str());
        return;
    }
    hvjs_promise_resolve(op, js_push_redis_reply(js, result.reply));
}

bool js_build_redis_command(JSContext* js, JSValueConst* argv, int argc, int first, RedisCommand* cmd) {
    if (argc <= first) return false;
    if (JS_IsArray(js, argv[first]) && argc == first + 1) {
        JSValue lenv = JS_GetPropertyStr(js, argv[first], "length");
        uint32_t len = 0;
        JS_ToUint32(js, &len, lenv);
        JS_FreeValue(js, lenv);
        // Cap the argument count: a sparse array can report length up to
        // 0xffffffff, and this native loop runs inside a C callback where the
        // interrupt handler cannot enforce the request timeout.
        const uint32_t MAX_REDIS_ARGS = 1024 * 1024;
        if (len > MAX_REDIS_ARGS) return false;
        for (uint32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(js, argv[first], i);
            cmd->push_back(hvjs_to_string(js, item));
            JS_FreeValue(js, item);
        }
    }
    else {
        for (int i = first; i < argc; ++i) {
            cmd->push_back(hvjs_to_string(js, argv[i]));
        }
    }
    return !cmd->empty();
}

const char* js_redis_verb_name(int magic) {
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

JSValue js_redis_command(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    HvJsRedisClient* box = js_redis_client(js, this_val);
    HvJsRedisState* state = box ? box->state.get() : NULL;
    if (state == NULL || !state->client || state->destroyed) {
        return hvjs_rejected_promise(js, "hv.redis: client closed");
    }
    RedisCommand cmd;
    if (magic != 0) {
        const char* verb = js_redis_verb_name(magic);
        if (verb == NULL) {
            return hvjs_rejected_promise(js, "hv.redis: unknown command");
        }
        cmd.push_back(verb);
        for (int i = 0; i < argc; ++i) {
            cmd.push_back(hvjs_to_string(js, argv[i]));
        }
    }
    else if (!js_build_redis_command(js, argv, argc, 0, &cmd)) {
        return hvjs_rejected_promise(js, "hv.redis: empty or invalid command");
    }

    HvJsTask* task = hvjs_get_task(js);
    HvJsRedisCommand* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsRedisCommand>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->redis = box->state;
    std::shared_ptr<HvJsPromiseOp*> handle = op->handle;
    ++task->in_call;
    int ret = state->client->command(cmd, [handle](const RedisResult& result) {
        HvJsPromiseOp* op = handle ? *handle : NULL;
        if (op == NULL) return;
        js_redis_resolve_result(static_cast<HvJsRedisCommand*>(op), result);
    });
    if (ret != 0) {
        hvjs_promise_reject(op, "hv.redis: request failed");
    }
    --task->in_call;
    hvjs_finish_deferred_ops(task);
    return promise;
}

JSValue js_redis_new(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    HvJsTask* task = hvjs_get_task(js);
    EventLoopPtr loop_ptr = currentThreadEventLoopPtr;
    if (task == NULL || !loop_ptr) {
        return JS_ThrowTypeError(js, "hv.redis: no shared event loop on this thread");
    }
    js_redis_register_class(js);

    std::string host = "127.0.0.1";
    int port = 6379;
    std::string auth;
    int db = 0;
    int timeout = 0;
    if (argc > 0 && JS_IsObject(argv[0])) {
        host = hvjs_get_string_property(js, argv[0], "host", "127.0.0.1");
        port = hvjs_get_int_property(js, argv[0], "port", 6379);
        auth = hvjs_get_string_property(js, argv[0], "auth", "");
        db = hvjs_get_int_property(js, argv[0], "db", 0);
        timeout = hvjs_get_int_property(js, argv[0], "timeout", 0);
    }

    JSValue obj = JS_NewObjectClass(js, s_redis_class_id);
    if (JS_IsException(obj)) return obj;
    HvJsRedisClient* box = new HvJsRedisClient();
    box->state = std::make_shared<HvJsRedisState>();
    box->state->client = std::make_shared<AsyncRedisClient>(loop_ptr);
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

} // namespace

JSValue hvjs_require_redis(JSContext* js) {
    JSValue redis = JS_NewObject(js);
    JS_SetPropertyStr(js, redis, "new", JS_NewCFunction(js, js_redis_new, "new", 1));
    return redis;
}

} // namespace js
} // namespace hv

#endif // HVJS_WITH_REDIS
#endif // WITH_JS
