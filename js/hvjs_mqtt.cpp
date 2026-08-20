#ifdef WITH_JS

#include "hvjs.h"

#ifdef HVJS_WITH_MQTT

#include <stdint.h>
#include <string.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "mqtt_client.h"

namespace hv {
namespace js {
namespace {

static JSClassID s_mqtt_class_id;
static std::once_flag s_mqtt_class_once;

struct HvJsMqttMessage {
    std::string topic;
    std::string payload;
    int qos;
};

struct HvJsMqttState {
    mqtt_client_t* client;
    std::deque<HvJsMqttMessage> inbox;
    HvJsPromiseOp* connect_op;
    HvJsPromiseOp* recv_op;
    bool js_alive;
    bool closed;
    bool reconnect;

    HvJsMqttState() : client(NULL), connect_op(NULL), recv_op(NULL), js_alive(false), closed(false), reconnect(false) {}

    void detach() {
        closed = true;
        if (client) {
            mqtt_client_set_callback(client, NULL);
            mqtt_client_set_userdata(client, NULL);
            mqtt_client_free(client);
            client = NULL;
        }
    }

    ~HvJsMqttState() { detach(); }
};

struct HvJsMqttClient {
    std::shared_ptr<HvJsMqttState> state;
};

struct HvJsMqttConnect : public HvJsPromiseOp {
    std::shared_ptr<HvJsMqttState> state;
};

struct HvJsMqttRecv : public HvJsPromiseOp {
    std::shared_ptr<HvJsMqttState> state;
};

struct HvJsMqttDetachEvent {
    std::shared_ptr<HvJsMqttState> state;
};

HvJsMqttClient* js_mqtt_client(JSContext* js, JSValueConst this_val) {
    return (HvJsMqttClient*)JS_GetOpaque2(js, this_val, s_mqtt_class_id);
}

JSValue js_mqtt_recv(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_mqtt_publish(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_mqtt_subscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_mqtt_unsubscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_mqtt_disconnect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv);
void js_mqtt_detach_after_callback(const EventLoopPtr& loop, hloop_t* raw_loop, const std::shared_ptr<HvJsMqttState>& state);

void js_mqtt_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    HvJsMqttClient* box = (HvJsMqttClient*)JS_GetOpaque(val, s_mqtt_class_id);
    if (box && box->state) {
        box->state->js_alive = false;
        if (box->state->connect_op == NULL && box->state->recv_op == NULL) {
            box->state->detach();
        }
    }
    delete box;
}

void js_mqtt_register_class(JSContext* js) {
    std::call_once(s_mqtt_class_once, []() { hvjs_new_class_id(&s_mqtt_class_id); });
    JSRuntime* rt = JS_GetRuntime(js);
    if (!JS_IsRegisteredClass(rt, s_mqtt_class_id)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = "hv.mqtt.client";
        def.finalizer = js_mqtt_finalizer;
        JS_NewClass(rt, s_mqtt_class_id, &def);
    }
}

JSValue js_mqtt_new_client_object(JSContext* js, const std::shared_ptr<HvJsMqttState>& state) {
    JSValue obj = JS_NewObjectClass(js, s_mqtt_class_id);
    if (JS_IsException(obj)) return obj;
    HvJsMqttClient* box = new HvJsMqttClient();
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

JSValue js_push_mqtt_message(JSContext* js, const HvJsMqttMessage& msg) {
    JSValue obj = JS_NewObject(js);
    JS_SetPropertyStr(js, obj, "topic", JS_NewStringLen(js, msg.topic.data(), msg.topic.size()));
    JS_SetPropertyStr(js, obj, "payload", JS_NewStringLen(js, msg.payload.data(), msg.payload.size()));
    JS_SetPropertyStr(js, obj, "qos", JS_NewInt32(js, msg.qos));
    return obj;
}

const char* js_mqtt_closed_reason(const HvJsMqttState* state) {
    return state && state->reconnect ? "reconnecting" : "closed";
}

void js_mqtt_try_deliver(HvJsMqttState* state) {
    if (!state || state->recv_op == NULL) return;
    HvJsMqttRecv* op = static_cast<HvJsMqttRecv*>(state->recv_op);
    std::shared_ptr<HvJsMqttState> hold = op->state;
    EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
    hloop_t* raw_loop = op->task ? op->task->loop : NULL;
    if (!state->inbox.empty()) {
        HvJsMqttMessage msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        state->recv_op = NULL;
        hvjs_promise_resolve(op, js_push_mqtt_message(op->task->js, msg));
    }
    else if (state->closed) {
        state->recv_op = NULL;
        hvjs_promise_reject(op, js_mqtt_closed_reason(state));
    }
    if (!hold->js_alive && hold->connect_op == NULL && hold->recv_op == NULL) {
        js_mqtt_detach_after_callback(loop, raw_loop, hold);
    }
}

void js_mqtt_detach_event_cb(hevent_t* ev) {
    HvJsMqttDetachEvent* detach = (HvJsMqttDetachEvent*)hevent_userdata(ev);
    if (detach) {
        detach->state->detach();
        delete detach;
    }
}

void js_mqtt_detach_after_callback(const EventLoopPtr& loop, hloop_t* raw_loop, const std::shared_ptr<HvJsMqttState>& state) {
    if (!state) return;
    state->reconnect = false;
    if (state->client) {
        mqtt_client_set_reconnect(state->client, NULL);
    }
    if (loop) {
        loop->queueInLoop([state]() { state->detach(); });
    }
    else if (raw_loop) {
        HvJsMqttDetachEvent* detach = new HvJsMqttDetachEvent();
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

void js_mqtt_on_event(mqtt_client_t* client, int type) {
    HvJsMqttState* state = (HvJsMqttState*)mqtt_client_get_userdata(client);
    if (state == NULL) return;
    switch (type) {
    case MQTT_TYPE_CONNACK:
        state->closed = false;
        if (state->connect_op) {
            HvJsMqttConnect* op = static_cast<HvJsMqttConnect*>(state->connect_op);
            std::shared_ptr<HvJsMqttState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->connect_op = NULL;
            JSValue obj = js_mqtt_new_client_object(op->task->js, hold);
            if (JS_IsException(obj)) {
                hvjs_promise_reject(op, "hv.mqtt: create client failed");
                js_mqtt_detach_after_callback(loop, raw_loop, hold);
            }
            else {
                hvjs_promise_resolve(op, obj);
            }
        }
        break;
    case MQTT_TYPE_PUBLISH: {
        HvJsMqttMessage msg;
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
            HvJsMqttConnect* op = static_cast<HvJsMqttConnect*>(state->connect_op);
            std::shared_ptr<HvJsMqttState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->connect_op = NULL;
            hvjs_promise_reject(op, "connect failed");
            js_mqtt_detach_after_callback(loop, raw_loop, hold);
        }
        if (state->recv_op) {
            HvJsMqttRecv* op = static_cast<HvJsMqttRecv*>(state->recv_op);
            std::shared_ptr<HvJsMqttState> hold = op->state;
            EventLoopPtr loop = op->task ? op->task->loop_ptr : EventLoopPtr();
            hloop_t* raw_loop = op->task ? op->task->loop : NULL;
            state->recv_op = NULL;
            hvjs_promise_reject(op, js_mqtt_closed_reason(state));
            if (!hold->js_alive && hold->connect_op == NULL && hold->recv_op == NULL) {
                js_mqtt_detach_after_callback(loop, raw_loop, hold);
            }
        }
        break;
    default: break;
    }
}

bool js_parse_reconnect(JSContext* js, JSValueConst obj, reconn_setting_t* out) {
    JSValue reconnect;
    if (!hvjs_get_property(js, obj, "reconnect", &reconnect) || !JS_IsObject(reconnect)) {
        if (!JS_IsUndefined(reconnect) && !JS_IsException(reconnect)) JS_FreeValue(js, reconnect);
        return false;
    }
    reconn_setting_init(out);
    out->min_delay = (uint32_t)hvjs_get_int_property(js, reconnect, "min_delay", (int)out->min_delay);
    out->max_delay = (uint32_t)hvjs_get_int_property(js, reconnect, "max_delay", (int)out->max_delay);
    out->delay_policy = (uint32_t)hvjs_get_int_property(js, reconnect, "delay_policy", (int)out->delay_policy);
    out->max_retry_cnt = (uint32_t)hvjs_get_int_property(js, reconnect, "max_retry", (int)out->max_retry_cnt);
    if (out->max_retry_cnt == 0) out->max_retry_cnt = INFINITE;
    if (out->min_delay == 0) out->min_delay = 1;
    if (out->max_delay < out->min_delay) out->max_delay = out->min_delay;
    if (out->delay_policy > 1 && out->delay_policy > UINT32_MAX / out->min_delay) {
        out->delay_policy = DEFAULT_RECONNECT_DELAY_POLICY;
    }
    JS_FreeValue(js, reconnect);
    return true;
}

JSValue js_mqtt_connect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    HvJsTask* task = hvjs_get_task(js);
    if (task == NULL || task->loop == NULL) {
        return hvjs_rejected_promise(js, "hv.mqtt: no event loop on this thread");
    }
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return hvjs_rejected_promise(js, "hv.mqtt: connect needs options");
    }

    std::string host = hvjs_get_string_property(js, argv[0], "host", "127.0.0.1");
    int port = hvjs_get_int_property(js, argv[0], "port", DEFAULT_MQTT_PORT);
    int ssl = hvjs_get_bool_property(js, argv[0], "ssl", false) ? 1 : 0;
    std::string id = hvjs_get_string_property(js, argv[0], "id", "");
    std::string username = hvjs_get_string_property(js, argv[0], "username", "");
    std::string password = hvjs_get_string_property(js, argv[0], "password", "");
    int keepalive = hvjs_get_int_property(js, argv[0], "keepalive", 0);
    int timeout = hvjs_get_int_property(js, argv[0], "connect_timeout", 0);
    if (timeout <= 0) timeout = hvjs_get_int_property(js, argv[0], "timeout", 0);
    bool clean_session = hvjs_get_bool_property(js, argv[0], "clean_session", true);

    js_mqtt_register_class(js);
    std::shared_ptr<HvJsMqttState> state = std::make_shared<HvJsMqttState>();
    state->client = mqtt_client_new(task->loop);
    if (state->client == NULL) {
        return hvjs_rejected_promise(js, "hv.mqtt: create client failed");
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

    HvJsMqttConnect* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsMqttConnect>(js, task, &op);
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
        hvjs_promise_reject(op, "hv.mqtt: connect failed");
        state->detach();
    }
    task->in_call = false;
    hvjs_finish_deferred_op(op);
    return promise;
}

JSValue js_mqtt_recv(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    HvJsMqttClient* box = js_mqtt_client(js, this_val);
    std::shared_ptr<HvJsMqttState> state = box ? box->state : std::shared_ptr<HvJsMqttState>();
    if (!state || state->client == NULL) {
        return hvjs_rejected_promise(js, "closed");
    }
    if (!state->inbox.empty()) {
        HvJsMqttMessage msg = std::move(state->inbox.front());
        state->inbox.pop_front();
        return hvjs_async_resolved_promise(js, hvjs_get_task(js), js_push_mqtt_message(js, msg));
    }
    if (state->closed) {
        return hvjs_rejected_promise(js, js_mqtt_closed_reason(state.get()));
    }
    if (state->recv_op != NULL) {
        return hvjs_rejected_promise(js, "hv.mqtt: recv already pending");
    }
    HvJsTask* task = hvjs_get_task(js);
    HvJsMqttRecv* op = NULL;
    JSValue promise = hvjs_new_promise<HvJsMqttRecv>(js, task, &op);
    if (JS_IsException(promise)) return promise;
    op->state = state;
    state->recv_op = op;
    return promise;
}

JSValue js_mqtt_publish(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    HvJsMqttClient* box = js_mqtt_client(js, this_val);
    HvJsMqttState* state = box ? box->state.get() : NULL;
    if (state == NULL || state->client == NULL || state->closed) {
        return JS_ThrowTypeError(js, "hv.mqtt: closed");
    }
    if (argc < 2) {
        return JS_ThrowTypeError(js, "hv.mqtt: publish needs topic and payload");
    }
    std::string topic = hvjs_to_string(js, argv[0]);
    std::string payload = hvjs_to_string(js, argv[1]);
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

JSValue js_mqtt_subscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    HvJsMqttClient* box = js_mqtt_client(js, this_val);
    HvJsMqttState* state = box ? box->state.get() : NULL;
    if (state == NULL || state->client == NULL || state->closed) {
        return JS_ThrowTypeError(js, "hv.mqtt: closed");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(js, "hv.mqtt: subscribe needs topic");
    }
    std::string topic = hvjs_to_string(js, argv[0]);
    int32_t qos = 0;
    if (argc > 1 && JS_ToInt32(js, &qos, argv[1]) != 0) return JS_EXCEPTION;
    int mid = mqtt_client_subscribe(state->client, topic.c_str(), qos);
    if (mid < 0) {
        return JS_ThrowInternalError(js, "hv.mqtt: subscribe failed");
    }
    return JS_NewInt32(js, mid);
}

JSValue js_mqtt_unsubscribe(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    HvJsMqttClient* box = js_mqtt_client(js, this_val);
    HvJsMqttState* state = box ? box->state.get() : NULL;
    if (state == NULL || state->client == NULL || state->closed) {
        return JS_ThrowTypeError(js, "hv.mqtt: closed");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(js, "hv.mqtt: unsubscribe needs topic");
    }
    std::string topic = hvjs_to_string(js, argv[0]);
    int mid = mqtt_client_unsubscribe(state->client, topic.c_str());
    if (mid < 0) {
        return JS_ThrowInternalError(js, "hv.mqtt: unsubscribe failed");
    }
    return JS_NewInt32(js, mid);
}

JSValue js_mqtt_disconnect(JSContext* js, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc;
    (void)argv;
    HvJsMqttClient* box = js_mqtt_client(js, this_val);
    if (box && box->state) {
        std::shared_ptr<HvJsMqttState> state = box->state;
        state->reconnect = false;
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

} // namespace

JSValue hvjs_require_mqtt(JSContext* js) {
    JSValue mqtt = JS_NewObject(js);
    JS_SetPropertyStr(js, mqtt, "connect", JS_NewCFunction(js, js_mqtt_connect, "connect", 1));
    return mqtt;
}

} // namespace js
} // namespace hv

#endif // HVJS_WITH_MQTT
#endif // WITH_JS
