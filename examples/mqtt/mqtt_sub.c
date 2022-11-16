/*
 * mqtt subscribe
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
#define TEST_RECONNECT  1

/*
 * workflow:
 * mqtt_client_new -> mqtt_client_xxx -> mqtt_client_run
 *
 * mqtt_client_set_xxx ->
 * mqtt_client_connect ->
 * on_connack -> mqtt_client_subscribe ->
 * on_publish -> handle_message
 *
 */

static void handle_message(mqtt_client_t* cli, mqtt_message_t* msg) {
    printf("topic: %.*s\n", msg->topic_len, msg->topic);
    printf("payload: %.*s\n", msg->payload_len, msg->payload);
}

static void on_mqtt(mqtt_client_t* cli, int type) {
    printf("on_mqtt type=%d\n", type);
    switch(type) {
    case MQTT_TYPE_CONNECT:
        printf("mqtt connected!\n");
        if (cli->reconn_setting && cli->reconn_setting->cur_retry_cnt > 0) {
            printf("mqtt reconnect cnt=%d, delay=%d\n", cli->reconn_setting->cur_retry_cnt, cli->reconn_setting->cur_delay);
        }
        break;
    case MQTT_TYPE_DISCONNECT:
        printf("mqtt disconnected!\n");
        if (cli->reconn_setting && cli->reconn_setting->cur_retry_cnt > 0) {
            printf("mqtt reconnect cnt=%d, delay=%d\n", cli->reconn_setting->cur_retry_cnt, cli->reconn_setting->cur_delay);
        }
        break;
    case MQTT_TYPE_CONNACK:
        printf("mqtt connack!\n");
    {
        const char* topic = (const char*)mqtt_client_get_userdata(cli);
        int mid = mqtt_client_subscribe(cli, topic, 0);
        printf("mqtt subscribe mid=%d\n", mid);
    }
        break;
    case MQTT_TYPE_SUBACK:
        printf("mqtt suback mid=%d\n", cli->mid);
        break;
    case MQTT_TYPE_PUBLISH:
        handle_message(cli, &cli->message);
    default:
        break;
    }
}

static int mqtt_subscribe(const char* host, int port, const char* topic) {
    mqtt_client_t* cli = mqtt_client_new(NULL);
    if (cli == NULL) return -1;
    cli->keepalive = 10;

#if TEST_AUTH
    mqtt_client_set_auth(cli, "test", "123456");
#endif

    mqtt_client_set_userdata(cli, (void*)topic);
    mqtt_client_set_callback(cli, on_mqtt);

#if TEST_RECONNECT
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 1000;
    reconn.max_delay = 10000;
    reconn.delay_policy = 2;
    mqtt_client_set_reconnect(cli, &reconn);
#endif

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
    if (argc < 4) {
        printf("Usage: %s host port topic\n", argv[0]);
        return -10;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* topic = argv[3];

    return mqtt_subscribe(host, port, topic);
}
