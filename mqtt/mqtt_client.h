#ifndef HV_MQTT_CLIENT_H_
#define HV_MQTT_CLIENT_H_

#include "mqtt_protocol.h"
#include "hloop.h"
#include "hssl.h"
#include "hmutex.h"

#define DEFAULT_MQTT_KEEPALIVE  60 // s

typedef struct mqtt_client_s mqtt_client_t;

// @type    mqtt_type_e
// @example examples/mqtt
typedef void (*mqtt_client_cb)(mqtt_client_t* cli, int type);

struct mqtt_client_s {
    // connect: host:port
    char host[256];
    int  port;
    int  connect_timeout; // ms
    // reconnect
    reconn_setting_t* reconn_setting;
    // login: flags + keepalive + client_id + will + username + password
    // flags
    unsigned char   protocol_version; // Default MQTT_PROTOCOL_V311
    unsigned char   clean_session:   1;
    unsigned char   ssl: 1; // Read Only
    unsigned char   alloced_ssl_ctx: 1; // intern
    unsigned char   connected : 1;
    unsigned short  keepalive;
    int             ping_cnt;
    char client_id[64];
    // will
    mqtt_message_t* will;
    // auth
    char username[64];
    char password[64];
    // message
    mqtt_head_t head;
    int error;              // for MQTT_TYPE_CONNACK
    int mid;                // for MQTT_TYPE_SUBACK, MQTT_TYPE_PUBACK
    mqtt_message_t message; // for MQTT_TYPE_PUBLISH
    // callback
    mqtt_client_cb cb;
    // userdata
    void* userdata;
    // privdata
    hloop_t*    loop;
    hio_t*      io;
    htimer_t*   reconn_timer;
    // SSL/TLS
    hssl_ctx_t ssl_ctx;
    // thread-safe
    hmutex_t mutex_;
};

BEGIN_EXTERN_C

// hloop_new -> malloc(mqtt_client_t)
HV_EXPORT mqtt_client_t* mqtt_client_new(hloop_t* loop DEFAULT(NULL));
// @see hloop_run
HV_EXPORT void           mqtt_client_run (mqtt_client_t* cli);
// @see hloop_stop
HV_EXPORT void           mqtt_client_stop(mqtt_client_t* cli);
// hloop_free -> free(mqtt_client_t)
HV_EXPORT void           mqtt_client_free(mqtt_client_t* cli);

// id
HV_EXPORT void mqtt_client_set_id(mqtt_client_t* cli, const char* id);

// will
HV_EXPORT void mqtt_client_set_will(mqtt_client_t* cli,
        mqtt_message_t* will);

// auth
HV_EXPORT void mqtt_client_set_auth(mqtt_client_t* cli,
        const char* username, const char* password);

// callback
HV_EXPORT void mqtt_client_set_callback(mqtt_client_t* cli, mqtt_client_cb cb);

// userdata
HV_EXPORT void  mqtt_client_set_userdata(mqtt_client_t* cli, void* userdata);
HV_EXPORT void* mqtt_client_get_userdata(mqtt_client_t* cli);

// error
HV_EXPORT int mqtt_client_get_last_error(mqtt_client_t* cli);

// SSL/TLS
HV_EXPORT int mqtt_client_set_ssl_ctx(mqtt_client_t* cli, hssl_ctx_t ssl_ctx);
// hssl_ctx_new(opt) -> mqtt_client_set_ssl_ctx
HV_EXPORT int mqtt_client_new_ssl_ctx(mqtt_client_t* cli, hssl_ctx_opt_t* opt);

// reconnect
HV_EXPORT int mqtt_client_set_reconnect(mqtt_client_t* cli,
        reconn_setting_t* reconn);
HV_EXPORT int mqtt_client_reconnect(mqtt_client_t* cli);

// connect
// hio_create_socket -> hio_connect ->
// on_connect -> mqtt_client_login ->
// on_connack
HV_EXPORT void mqtt_client_set_connect_timeout(mqtt_client_t* cli, int ms);
HV_EXPORT int  mqtt_client_connect(mqtt_client_t* cli,
        const char* host,
        int port DEFAULT(DEFAULT_MQTT_PORT),
        int ssl  DEFAULT(0));
HV_EXPORT bool mqtt_client_is_connected(mqtt_client_t* cli);

// disconnect
// @see hio_close
HV_EXPORT int mqtt_client_disconnect(mqtt_client_t* cli);

// publish
HV_EXPORT int mqtt_client_publish(mqtt_client_t* cli,
        mqtt_message_t* msg);

// subscribe
HV_EXPORT int mqtt_client_subscribe(mqtt_client_t* cli,
        const char* topic, int qos DEFAULT(0));

// unsubscribe
HV_EXPORT int mqtt_client_unsubscribe(mqtt_client_t* cli,
        const char* topic);

END_EXTERN_C

#ifdef __cplusplus

#include <functional>
#include <map>
#include <mutex>

namespace hv {

// @usage examples/mqtt/mqtt_client_test.cpp
class MqttClient {
public:
    mqtt_client_t*  client;
    // callbacks
    typedef std::function<void(MqttClient*)>                    MqttCallback;
    typedef std::function<void(MqttClient*, mqtt_message_t*)>   MqttMessageCallback;
    MqttCallback        onConnect;
    MqttCallback        onClose;
    MqttMessageCallback onMessage;

    MqttClient(hloop_t* loop = NULL) {
        client = mqtt_client_new(loop);
    }

    ~MqttClient() {
        if (client) {
            mqtt_client_free(client);
            client = NULL;
        }
    }

    void run() {
        mqtt_client_set_callback(client, on_mqtt);
        mqtt_client_set_userdata(client, this);
        mqtt_client_run(client);
    }

    void stop() {
        mqtt_client_stop(client);
    }

    void setID(const char* id) {
        mqtt_client_set_id(client, id);
    }

    void setWill(mqtt_message_t* will) {
        mqtt_client_set_will(client, will);
    }

    void setAuth(const char* username, const char* password) {
        mqtt_client_set_auth(client, username, password);
    }

    void setPingInterval(int sec) {
        client->keepalive = sec;
    }

    int lastError() {
        return mqtt_client_get_last_error(client);
    }

    // SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx) {
        return mqtt_client_set_ssl_ctx(client, ssl_ctx);
    }
    int newSslCtx(hssl_ctx_opt_t* opt) {
        return mqtt_client_new_ssl_ctx(client, opt);
    }

    void setReconnect(reconn_setting_t* reconn) {
        mqtt_client_set_reconnect(client, reconn);
    }

    void setConnectTimeout(int ms) {
        mqtt_client_set_connect_timeout(client, ms);
    }

    int connect(const char* host, int port = DEFAULT_MQTT_PORT, int ssl = 0) {
        return mqtt_client_connect(client, host, port, ssl);
    }

    int reconnect() {
        return mqtt_client_reconnect(client);
    }

    int disconnect() {
        return mqtt_client_disconnect(client);
    }

    bool isConnected() {
        return mqtt_client_is_connected(client);
    }

    int publish(mqtt_message_t* msg, MqttCallback ack_cb = NULL) {
        int mid = mqtt_client_publish(client, msg);
        if (msg->qos > 0 && mid >= 0 && ack_cb) {
            setAckCallback(mid, ack_cb);
        }
        return mid;
    }

    int publish(const std::string& topic, const std::string& payload, int qos = 0, int retain = 0, MqttCallback ack_cb = NULL) {
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.topic_len = topic.size();
        msg.topic = topic.c_str();
        msg.payload_len = payload.size();
        msg.payload = payload.c_str();
        msg.qos = qos;
        msg.retain = retain;
        return publish(&msg, ack_cb);
    }

    int subscribe(const char* topic, int qos = 0, MqttCallback ack_cb = NULL) {
        int mid = mqtt_client_subscribe(client, topic, qos);
        if (qos > 0 && mid >= 0 && ack_cb) {
            setAckCallback(mid, ack_cb);
        }
        return mid;
    }

    int unsubscribe(const char* topic, MqttCallback ack_cb = NULL) {
        int mid = mqtt_client_unsubscribe(client, topic);
        if (mid >= 0 && ack_cb) {
            setAckCallback(mid, ack_cb);
        }
        return mid;
    }

protected:
    void setAckCallback(int mid, MqttCallback cb) {
        ack_cbs_mutex.lock();
        ack_cbs[mid] = std::move(cb);
        ack_cbs_mutex.unlock();
    }

    void invokeAckCallback(int mid) {
        MqttCallback ack_cb = NULL;
        ack_cbs_mutex.lock();
        auto iter = ack_cbs.find(mid);
        if (iter != ack_cbs.end()) {
            ack_cb = std::move(iter->second);
            ack_cbs.erase(iter);
        }
        ack_cbs_mutex.unlock();
        if (ack_cb) ack_cb(this);
    }

    static void on_mqtt(mqtt_client_t* cli, int type) {
        MqttClient* client = (MqttClient*)mqtt_client_get_userdata(cli);
        // printf("on_mqtt type=%d\n", type);
        switch(type) {
        case MQTT_TYPE_CONNECT:
            // printf("mqtt connected!\n");
            break;
        case MQTT_TYPE_DISCONNECT:
            // printf("mqtt disconnected!\n");
            if (client->onClose) {
                client->onClose(client);
            }
            break;
        case MQTT_TYPE_CONNACK:
            // printf("mqtt connack!\n");
            if (client->onConnect) {
                client->onConnect(client);
            }
            break;
        case MQTT_TYPE_PUBLISH:
            if (client->onMessage) {
                client->onMessage(client, &cli->message);
            }
            break;
        case MQTT_TYPE_PUBACK: /* qos = 1 */
            // printf("mqtt puback mid=%d\n", cli->mid);
            client->invokeAckCallback(cli->mid);
            break;
        case MQTT_TYPE_PUBREC: /* qos = 2 */
            // printf("mqtt pubrec mid=%d\n", cli->mid);
            // wait MQTT_TYPE_PUBCOMP
            break;
        case MQTT_TYPE_PUBCOMP: /* qos = 2 */
            // printf("mqtt pubcomp mid=%d\n", cli->mid);
            client->invokeAckCallback(cli->mid);
            break;
        case MQTT_TYPE_SUBACK:
            // printf("mqtt suback mid=%d\n", cli->mid);
            client->invokeAckCallback(cli->mid);
            break;
        case MQTT_TYPE_UNSUBACK:
            // printf("mqtt unsuback mid=%d\n", cli->mid);
            client->invokeAckCallback(cli->mid);
            break;
        default:
            break;
        }
    }

private:
    // mid => ack callback
    std::map<int, MqttCallback> ack_cbs;
    std::mutex                  ack_cbs_mutex;
};

}
#endif

#endif // HV_MQTT_CLIENT_H_
