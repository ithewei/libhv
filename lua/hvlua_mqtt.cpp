extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hvlua.h"
#include "hvlua_util.h"   // hvlua_parse_reconnect

#ifdef HVLUA_WITH_MQTT

#include <cstring>
#include <deque>
#include <string>

#include "EventLoop.h"
#include "mqtt_client.h"

using namespace hv;

// hv.mqtt — coroutine-synchronous MQTT client. Single-loop model (mirrors
// hvlua_ws.cpp): the mqtt_client_t is created on the CURRENT loop's hloop_t, so
// its callbacks fire on this same loop thread and resume the coroutine directly.
//
// NOTE: this binds the raw C API (mqtt_client_t) rather than the C++ MqttClient
// wrapper on purpose. MqttClient::run() is what installs the dispatch callback,
// but run() also calls hloop_run() (blocks) — we must NOT run the shared loop
// here. So we install our own mqtt_client_cb via mqtt_client_set_callback and
// drive connect/publish/subscribe on the already-running shared loop.
//
// MQTT is message-DRIVEN (broker pushes PUBLISH anytime), so like hv.ws we
// buffer inbound messages and expose a coroutine-synchronous recv():
//   local m, err = hv.mqtt.connect({ host=, port=, id=, username=, password=,
//                                    keepalive=, clean_session=, ssl= })
//   m:subscribe("topic", 1)
//   m:publish("topic", "payload", 1)
//   local msg = m:recv()   -- { topic=, payload=, qos= } ; suspends until a msg
//   m:disconnect()

static const char* MQTT_CLIENT_MT = "hv.mqtt.client.mt";

struct MqttInboxItem {
    std::string topic;
    std::string payload;
    int qos;
};
typedef std::deque<MqttInboxItem> MqttInbox;

struct LuaMqttClient {
    mqtt_client_t*  client;
    MqttInbox       inbox;
    HvLuaCoroutine* wait_co;   // coroutine waiting in connect() or recv(), or NULL
    bool            connected;
    bool            closed;
    bool            connecting;  // wait_co holds a connect() waiter (vs recv())
    bool            reconnect;   // auto-reconnect enabled
};

// Push an inbox item as a Lua table { topic=, payload=, qos= }.
static void mqtt_push_msg(lua_State* L, const MqttInboxItem& item) {
    lua_createtable(L, 0, 3);
    lua_pushlstring(L, item.topic.data(), item.topic.size());
    lua_setfield(L, -2, "topic");
    lua_pushlstring(L, item.payload.data(), item.payload.size());
    lua_setfield(L, -2, "payload");
    lua_pushinteger(L, item.qos);
    lua_setfield(L, -2, "qos");
}

// Wake a pending recv() with the front queued message, or an error when the
// connection is gone: (nil,"reconnecting") if auto-reconnect is on (transient)
// else (nil,"closed") (terminal). No-op for a connect() waiter.
static void mqtt_try_deliver(LuaMqttClient* box) {
    if (box->wait_co == NULL || box->connecting) return;
    lua_State* co = hvlua_coroutine_state(box->wait_co);
    if (co == NULL) { hvlua_cancel(box->wait_co); box->wait_co = NULL; return; }
    if (!box->inbox.empty()) {
        MqttInboxItem item = std::move(box->inbox.front());
        box->inbox.pop_front();
        HvLuaCoroutine* tok = box->wait_co;
        box->wait_co = NULL;
        mqtt_push_msg(co, item);
        hvlua_resume(tok, 1);
    } else if (box->closed) {
        HvLuaCoroutine* tok = box->wait_co;
        box->wait_co = NULL;
        lua_pushnil(co);
        lua_pushstring(co, box->reconnect ? "reconnecting" : "closed");
        hvlua_resume(tok, 2);
    }
}

// The single mqtt_client_cb: dispatched by type. Installed via
// mqtt_client_set_callback; the LuaMqttClient box is the client userdata.
static void on_mqtt(mqtt_client_t* cli, int type) {
    LuaMqttClient* box = (LuaMqttClient*)mqtt_client_get_userdata(cli);
    if (box == NULL) return;
    switch (type) {
    case MQTT_TYPE_CONNACK:
        box->connected = true;
        box->closed = false;   // reset for a (re)established session
        if (box->wait_co && box->connecting) {
            lua_State* co = hvlua_coroutine_state(box->wait_co);
            if (co == NULL) { hvlua_cancel(box->wait_co); box->wait_co = NULL; return; }
            HvLuaCoroutine* tok = box->wait_co;
            box->wait_co = NULL;
            box->connecting = false;
            lua_pushboolean(co, 1);      // success marker for mqtt_connect_k
            hvlua_resume(tok, 1);
        }
        break;
    case MQTT_TYPE_PUBLISH: {
        MqttInboxItem item;
        item.topic.assign(cli->message.topic, cli->message.topic_len);
        item.payload.assign(cli->message.payload, cli->message.payload_len);
        item.qos = cli->message.qos;
        box->inbox.push_back(std::move(item));
        mqtt_try_deliver(box);
        break;
    }
    case MQTT_TYPE_DISCONNECT:
        box->closed = true;
        box->connected = false;
        if (box->wait_co) {
            lua_State* co = hvlua_coroutine_state(box->wait_co);
            if (co == NULL) { hvlua_cancel(box->wait_co); box->wait_co = NULL; return; }
            HvLuaCoroutine* tok = box->wait_co;
            bool was_connecting = box->connecting;
            box->wait_co = NULL;
            box->connecting = false;
            lua_pushnil(co);
            // connect() waiter -> connect failed; recv() waiter -> transient
            // "reconnecting" if auto-reconnect is on (the C client retries under
            // the hood and a future CONNACK/PUBLISH wakes fresh recvs), else
            // terminal "closed".
            lua_pushstring(co, was_connecting ? "connect failed"
                                              : (box->reconnect ? "reconnecting" : "closed"));
            hvlua_resume(tok, 2);
        }
        break;
    default:
        break;
    }
}

static int mqtt_client_gc(lua_State* L) {
    LuaMqttClient* box = (LuaMqttClient*)luaL_checkudata(L, 1, MQTT_CLIENT_MT);
    if (box) {
        if (box->wait_co) {
            hvlua_cancel(box->wait_co);
            box->wait_co = NULL;
        }
        if (box->client) {
            // detach userdata so a late callback can't touch the freed box, then
            // free the client. mqtt_client_free does NOT stop the shared loop.
            mqtt_client_set_userdata(box->client, NULL);
            mqtt_client_free(box->client);
            box->client = NULL;
        }
        box->inbox.~MqttInbox();
    }
    return 0;
}

// Continuation for connect: (m) on success, or (nil,err) already on stack.
static int mqtt_connect_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, 1);       // return the mqtt userdata (self)
        return 1;
    }
    return 2;                      // (nil, err)
}

// hv.mqtt.connect(cfg) -> client | nil, err
static int l_mqtt_connect(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    // NOTE: lua_yieldk at the end never returns to this C++ frame (longjmp in a
    // C-built Lua), so destructors of non-trivial locals here are SKIPPED and
    // leak. All non-trivial locals (the EventLoopPtr and the std::strings from
    // the config) are therefore confined to the scope below, which ends BEFORE
    // hvlua_suspend/lua_yieldk. Only POD state crosses the yield.
    LuaMqttClient* box = NULL;
    char host[256] = "127.0.0.1";
    int port = DEFAULT_MQTT_PORT;
    int ssl = 0;
    {
        EventLoopPtr loop = currentThreadEventLoopPtr;
        if (!loop) {
            lua_pushnil(L);
            lua_pushstring(L, "hv.mqtt: no shared event loop on this thread");
            return 2;
        }

        std::string id, username, password;
        int keepalive = 0, clean_session = -1;
        lua_getfield(L, 1, "host");     if (lua_isstring(L, -1)) { strncpy(host, lua_tostring(L, -1), sizeof(host) - 1); host[sizeof(host) - 1] = '\0'; } lua_pop(L, 1);
        lua_getfield(L, 1, "port");     if (lua_isinteger(L, -1)) port = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, 1, "id");       if (lua_isstring(L, -1)) id = lua_tostring(L, -1);          lua_pop(L, 1);
        lua_getfield(L, 1, "username"); if (lua_isstring(L, -1)) username = lua_tostring(L, -1);    lua_pop(L, 1);
        lua_getfield(L, 1, "password"); if (lua_isstring(L, -1)) password = lua_tostring(L, -1);    lua_pop(L, 1);
        lua_getfield(L, 1, "keepalive");if (lua_isinteger(L, -1)) keepalive = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, 1, "ssl");      if (lua_isboolean(L, -1)) ssl = lua_toboolean(L, -1);       lua_pop(L, 1);
        lua_getfield(L, 1, "clean_session"); if (lua_isboolean(L, -1)) clean_session = lua_toboolean(L, -1); lua_pop(L, 1);

        box = (LuaMqttClient*)lua_newuserdata(L, sizeof(LuaMqttClient));
        new (&box->inbox) MqttInbox();
        box->wait_co = NULL;
        box->connected = false;
        box->closed = false;
        box->connecting = true;
        box->reconnect = false;
        box->client = mqtt_client_new(loop->loop());   // bound to current loop's hloop
        if (box->client == NULL) {
            box->inbox.~MqttInbox();
            lua_pushnil(L);
            lua_pushstring(L, "hv.mqtt: create client failed");
            return 2;
        }
        luaL_setmetatable(L, MQTT_CLIENT_MT);
        lua_replace(L, 1);              // move userdata to slot 1 for mqtt_connect_k

        mqtt_client_set_userdata(box->client, box);
        mqtt_client_set_callback(box->client, on_mqtt);
        if (!id.empty()) mqtt_client_set_id(box->client, id.c_str());
        if (!username.empty() || !password.empty()) {
            mqtt_client_set_auth(box->client, username.c_str(), password.c_str());
        }
        if (keepalive > 0) box->client->keepalive = (unsigned short)keepalive;
        if (clean_session >= 0) box->client->clean_session = clean_session ? 1 : 0;
        // optional reconnect = { min_delay, max_delay, delay_policy, max_retry }
        reconn_setting_t reconn;
        if (hvlua_parse_reconnect(L, 1, &reconn)) {
            mqtt_client_set_reconnect(box->client, &reconn);
            box->reconnect = true;
        }
    }   // ~loop / ~id / ~username / ~password run here, before the yield

    box->wait_co = hvlua_suspend(L);
    int ret = mqtt_client_connect(box->client, host, port, ssl);
    if (ret != 0) {
        hvlua_cancel(box->wait_co);
        box->wait_co = NULL;
        box->connecting = false;
        lua_pushnil(L);
        lua_pushfstring(L, "hv.mqtt: connect failed (%d)", ret);
        return 2;
    }
    return lua_yieldk(L, 0, (lua_KContext)0, mqtt_connect_k);
}

static int mqtt_recv_k(lua_State* L, int status, lua_KContext ctx) {
    (void)status; (void)ctx;
    return lua_gettop(L) >= 2 && lua_isnil(L, -2) ? 2 : 1;
}

// m:recv() -> { topic=, payload=, qos= } | nil, err  (coroutine-synchronous)
static int l_mqtt_recv(lua_State* L) {
    LuaMqttClient* box = (LuaMqttClient*)luaL_checkudata(L, 1, MQTT_CLIENT_MT);
    if (box == NULL || box->client == NULL) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    if (box->wait_co != NULL) {
        lua_pushnil(L); lua_pushstring(L, "hv.mqtt: recv already pending"); return 2;
    }
    if (!box->inbox.empty()) {
        MqttInboxItem item = std::move(box->inbox.front());
        box->inbox.pop_front();
        mqtt_push_msg(L, item);
        return 1;
    }
    if (box->closed) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    box->connecting = false;
    box->wait_co = hvlua_suspend(L);
    return lua_yieldk(L, 0, (lua_KContext)0, mqtt_recv_k);
}

// m:publish(topic, payload [, qos [, retain]]) -> mid | nil, err
static int l_mqtt_publish(lua_State* L) {
    LuaMqttClient* box = (LuaMqttClient*)luaL_checkudata(L, 1, MQTT_CLIENT_MT);
    size_t tlen = 0, plen = 0;
    const char* topic = luaL_checklstring(L, 2, &tlen);
    const char* payload = luaL_checklstring(L, 3, &plen);
    int qos = (int)luaL_optinteger(L, 4, 0);
    int retain = (int)luaL_optinteger(L, 5, 0);
    if (box == NULL || box->client == NULL || box->closed) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.topic = topic;         msg.topic_len = (unsigned int)tlen;
    msg.payload = payload;     msg.payload_len = (unsigned int)plen;
    msg.qos = (unsigned char)qos;
    msg.retain = (unsigned char)retain;
    int mid = mqtt_client_publish(box->client, &msg);
    if (mid < 0) {
        lua_pushnil(L); lua_pushstring(L, "hv.mqtt: publish failed"); return 2;
    }
    lua_pushinteger(L, mid);
    return 1;
}

// m:subscribe(topic [, qos]) -> mid | nil, err
static int l_mqtt_subscribe(lua_State* L) {
    LuaMqttClient* box = (LuaMqttClient*)luaL_checkudata(L, 1, MQTT_CLIENT_MT);
    const char* topic = luaL_checkstring(L, 2);
    int qos = (int)luaL_optinteger(L, 3, 0);
    if (box == NULL || box->client == NULL || box->closed) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    int mid = mqtt_client_subscribe(box->client, topic, qos);
    if (mid < 0) {
        lua_pushnil(L); lua_pushstring(L, "hv.mqtt: subscribe failed"); return 2;
    }
    lua_pushinteger(L, mid);
    return 1;
}

// m:unsubscribe(topic) -> mid | nil, err
static int l_mqtt_unsubscribe(lua_State* L) {
    LuaMqttClient* box = (LuaMqttClient*)luaL_checkudata(L, 1, MQTT_CLIENT_MT);
    const char* topic = luaL_checkstring(L, 2);
    if (box == NULL || box->client == NULL || box->closed) {
        lua_pushnil(L); lua_pushstring(L, "closed"); return 2;
    }
    int mid = mqtt_client_unsubscribe(box->client, topic);
    if (mid < 0) {
        lua_pushnil(L); lua_pushstring(L, "hv.mqtt: unsubscribe failed"); return 2;
    }
    lua_pushinteger(L, mid);
    return 1;
}

// m:disconnect()
static int l_mqtt_disconnect(lua_State* L) {
    LuaMqttClient* box = (LuaMqttClient*)luaL_checkudata(L, 1, MQTT_CLIENT_MT);
    if (box && box->client && !box->closed) {
        // Explicit disconnect is terminal: mqtt_client_disconnect also cancels
        // the underlying reconnect; clear our flag so recv() reports "closed"
        // (not "reconnecting").
        box->reconnect = false;
        mqtt_client_disconnect(box->client);
    }
    return 0;
}

static const luaL_Reg mqtt_methods[] = {
    { "recv",        l_mqtt_recv        },
    { "publish",     l_mqtt_publish     },
    { "subscribe",   l_mqtt_subscribe   },
    { "unsubscribe", l_mqtt_unsubscribe },
    { "disconnect",  l_mqtt_disconnect  },
    { NULL, NULL }
};

static const luaL_Reg mqtt_funcs[] = {
    { "connect", l_mqtt_connect },
    { NULL, NULL }
};

extern "C" void hvlua_open_mqtt(lua_State* L) {
    hvlua_new_class(L, MQTT_CLIENT_MT, mqtt_client_gc, mqtt_methods);
    lua_pop(L, 1);

    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_newlib(L, mqtt_funcs);
    lua_setfield(L, -2, "mqtt");
    lua_setglobal(L, "hv");
}

#endif // HVLUA_WITH_MQTT
