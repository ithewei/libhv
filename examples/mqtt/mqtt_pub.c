/*
 * mqtt publish
 *
 * @build   make examples
 * @sub     bin/mqtt_sub 127.0.0.1 1883 topic
 * @pub     bin/mqtt_pub 127.0.0.1 1883 topic payload
 *
 */

#include "hv.h"
#include "mqtt_client.h"

/*
 * @test    MQTTS
 * #define  TEST_SSL 1
 *
 * @build   ./configure --with-mqtt --with-openssl && make clean && make
 *
 */
#define TEST_SSL        0
#define TEST_AUTH       0

/*
 * workflow:
 * mqtt_client_new -> mqtt_client_xxx -> mqtt_client_run
 *
 * mqtt_client_set_xxx ->
 * mqtt_client_connect ->
 * on_connack -> mqtt_client_publish ->
 * on_puback -> mqtt_client_disconnect ->
 * on_disconnect -> mqtt_client_stop
 *
 */

static void on_mqtt(mqtt_client_t* cli, int type) {
    printf("on_mqtt type=%d\n", type);
    switch(type) {
    case MQTT_TYPE_CONNECT:
        printf("mqtt connected!\n");
        break;
    case MQTT_TYPE_DISCONNECT:
        printf("mqtt disconnected!\n");
    {
        mqtt_message_t* msg = (mqtt_message_t*)mqtt_client_get_userdata(cli);
        HV_FREE(msg);
        mqtt_client_set_userdata(cli, NULL);
        mqtt_client_stop(cli);
    }
        break;
    case MQTT_TYPE_CONNACK:
        printf("mqtt connack!\n");
    {
        mqtt_message_t* msg = (mqtt_message_t*)mqtt_client_get_userdata(cli);
        if (msg == NULL) return;
        int mid = mqtt_client_publish(cli, msg);
        printf("mqtt publish mid=%d\n", mid);
        if (msg->qos == 0) {
            mqtt_client_disconnect(cli);
        } else if (msg->qos == 1) {
            // wait MQTT_TYPE_PUBACK
        } else if (msg->qos == 2) {
            // wait MQTT_TYPE_PUBREC
        }
    }
        break;
    case MQTT_TYPE_PUBACK: /* qos = 1 */
        printf("mqtt puback mid=%d\n", cli->mid);
        mqtt_client_disconnect(cli);
        break;
    case MQTT_TYPE_PUBREC: /* qos = 2 */
        printf("mqtt pubrec mid=%d\n", cli->mid);
        // wait MQTT_TYPE_PUBCOMP
        break;
    case MQTT_TYPE_PUBCOMP: /* qos = 2 */
        printf("mqtt pubcomp mid=%d\n", cli->mid);
        mqtt_client_disconnect(cli);
        break;
    default:
        break;
    }
}

static int mqtt_publish(const char* host, int port, const char* topic, const char* payload) {
    mqtt_client_t* cli = mqtt_client_new(NULL);
    if (cli == NULL) return -1;
    cli->keepalive = 10;

    // client_id
    char client_id[64];
    snprintf(client_id, sizeof(client_id), "mqtt_pub_%ld", hv_getpid());
    printf("client_id: %s\n", client_id);
    mqtt_client_set_id(cli, client_id);
    // will
    mqtt_message_t will;
    memset(&will, 0, sizeof(will));
    will.topic = "will";
    will.payload = "This is a will.";
    mqtt_client_set_will(cli, &will);
#if TEST_AUTH
    mqtt_client_set_auth(cli, "test", "123456");
#endif

    mqtt_message_t* msg = NULL;
    HV_ALLOC_SIZEOF(msg);
    msg->topic = topic;
    msg->topic_len = strlen(topic);
    msg->payload = payload;
    msg->payload_len = strlen(payload);
    msg->qos = 1;
    mqtt_client_set_userdata(cli, msg);
    mqtt_client_set_callback(cli, on_mqtt);

    int ssl = 0;
#if TEST_SSL
    ssl = 1;
#endif
    mqtt_client_connect(cli, host, port, ssl);
    mqtt_client_run(cli);
    mqtt_client_free(cli);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: %s host port topic payload\n", argv[0]);
        return -10;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* topic = argv[3];
    const char* payload = argv[4];

    return mqtt_publish(host, port, topic, payload);
}
