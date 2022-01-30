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
    // reconnect
    reconn_setting_t* reconn_setting;
    // login: flags + keepalive + client_id + will + username + password
    // flags
    unsigned char   protocol_version; // Default MQTT_PROTOCOL_V311
    unsigned char   clean_session:   1;
    unsigned char   ssl: 1; // Read Only
    unsigned char   alloced_ssl_ctx: 1; // intern
    unsigned short  keepalive;
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
HV_EXPORT int mqtt_client_connect(mqtt_client_t* cli,
        const char* host,
        int port DEFAULT(DEFAULT_MQTT_PORT),
        int ssl  DEFAULT(0));

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

#endif // HV_MQTT_CLIENT_H_
