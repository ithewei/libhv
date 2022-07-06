#include "mqtt_client.h"
#include "hbase.h"
#include "hlog.h"
#include "herr.h"
#include "hendian.h"

static unsigned short mqtt_next_mid() {
    static unsigned short s_mid = 0;
    return ++s_mid;
}

static int mqtt_client_send(mqtt_client_t* cli, const void* buf, int len) {
    // thread-safe
    hmutex_lock(&cli->mutex_);
    int nwrite = hio_write(cli->io, buf, len);
    hmutex_unlock(&cli->mutex_);
    return nwrite;
}

static int mqtt_send_head(hio_t* io, int type, int length) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(io);
    mqtt_head_t head;
    memset(&head, 0, sizeof(head));
    head.type = type;
    head.length = length;
    unsigned char headbuf[8] = { 0 };
    int headlen = mqtt_head_pack(&head, headbuf);
    return mqtt_client_send(cli, headbuf, headlen);
}

static int mqtt_send_head_with_mid(hio_t* io, int type, unsigned short mid) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(io);
    mqtt_head_t head;
    memset(&head, 0, sizeof(head));
    head.type = type;
    if (head.type == MQTT_TYPE_PUBREL) {
        head.qos = 1;
    }
    head.length = 2;
    unsigned char headbuf[8] = { 0 };
    unsigned char* p = headbuf;
    int headlen = mqtt_head_pack(&head, p);
    p += headlen;
    PUSH16(p, mid);
    return mqtt_client_send(cli, headbuf, headlen + 2);
}

static void mqtt_send_ping(hio_t* io) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(io);
    if (++cli->ping_cnt > 3) {
        hloge("mqtt no pong!");
        hio_close(io);
        return;
    }
    mqtt_send_head(io, MQTT_TYPE_PINGREQ, 0);
}

static void mqtt_send_pong(hio_t* io) {
    mqtt_send_head(io, MQTT_TYPE_PINGRESP, 0);
}

static void mqtt_send_disconnect(hio_t* io) {
    mqtt_send_head(io, MQTT_TYPE_DISCONNECT, 0);
}

/*
 * MQTT_TYPE_CONNECT
 * 2 + protocol_name + 1 protocol_version + 1 conn_flags + 2 keepalive + 2 + [client_id] +
 * [2 + will_topic + 2 + will_payload] +
 * [2 + username] + [2 + password]
 */
static int mqtt_client_login(mqtt_client_t* cli) {
    int len = 2 + 1 + 1 + 2 + 2;
    unsigned short cid_len = 0,
                   will_topic_len = 0,
                   will_payload_len = 0,
                   username_len = 0,
                   password_len = 0;
    unsigned char conn_flags = 0;

    // protocol_name_len
    len += cli->protocol_version == MQTT_PROTOCOL_V31 ? 6 : 4;
    if (*cli->client_id) {
        cid_len = strlen(cli->client_id);
    } else {
        cid_len = 20;
        hv_random_string(cli->client_id, cid_len);
        hlogi("MQTT client_id: %.*s", (int)cid_len, cli->client_id);
    }
    len += cid_len;
    if (cid_len == 0) cli->clean_session = 1;
    if (cli->clean_session) {
        conn_flags |= MQTT_CONN_CLEAN_SESSION;
    }
    if (cli->will && cli->will->topic && cli->will->payload) {
        will_topic_len = cli->will->topic_len ? cli->will->topic_len : strlen(cli->will->topic);
        will_payload_len = cli->will->payload_len ? cli->will->payload_len : strlen(cli->will->payload);
        if (will_topic_len && will_payload_len) {
            conn_flags |= MQTT_CONN_HAS_WILL;
            conn_flags |= ((cli->will->qos & 3) << 3);
            if (cli->will->retain) {
                conn_flags |= MQTT_CONN_WILL_RETAIN;
            }
            len += 2 + will_topic_len;
            len += 2 + will_payload_len;
        }
    }
    if (*cli->username) {
        username_len = strlen(cli->username);
        if (username_len) {
            conn_flags |= MQTT_CONN_HAS_USERNAME;
            len += 2 + username_len;
        }
    }
    if (*cli->password) {
        password_len = strlen(cli->password);
        if (password_len) {
            conn_flags |= MQTT_CONN_HAS_PASSWORD;
            len += 2 + password_len;
        }
    }

    mqtt_head_t head;
    memset(&head, 0, sizeof(head));
    head.type = MQTT_TYPE_CONNECT;
    head.length = len;
    int buflen = mqtt_estimate_length(&head);
    unsigned char* buf = NULL;
    HV_STACK_ALLOC(buf, buflen);
    unsigned char* p = buf;
    int headlen = mqtt_head_pack(&head, p);
    p += headlen;
    // TODO: Not implement MQTT_PROTOCOL_V5
    if (cli->protocol_version == MQTT_PROTOCOL_V31) {
        PUSH16(p, 6);
        PUSH_N(p, MQTT_PROTOCOL_NAME_v31, 6);
    } else {
        PUSH16(p, 4);
        PUSH_N(p, MQTT_PROTOCOL_NAME, 4);
    }
    PUSH8(p, cli->protocol_version);
    PUSH8(p, conn_flags);
    PUSH16(p, cli->keepalive);
    PUSH16(p, cid_len);
    if (cid_len > 0) {
        PUSH_N(p, cli->client_id, cid_len);
    }
    if (conn_flags & MQTT_CONN_HAS_WILL) {
        PUSH16(p, will_topic_len);
        PUSH_N(p, cli->will->topic, will_topic_len);
        PUSH16(p, will_payload_len);
        PUSH_N(p, cli->will->payload, will_payload_len);
    }
    if (conn_flags & MQTT_CONN_HAS_USERNAME) {
        PUSH16(p, username_len);
        PUSH_N(p, cli->username, username_len);
    }
    if (conn_flags & MQTT_CONN_HAS_PASSWORD) {
        PUSH16(p, password_len);
        PUSH_N(p, cli->password, password_len);
    }

    int nwrite = mqtt_client_send(cli, buf, p - buf);
    HV_STACK_FREE(buf);
    return nwrite < 0 ? nwrite : 0;
}

static void reconnect_timer_cb(htimer_t* timer) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(timer);
    if (cli == NULL) return;
    cli->reconn_timer = NULL;
    mqtt_client_reconnect(cli);
}

static void on_close(hio_t* io) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(io);
    cli->connected = 0;
    if (cli->cb) {
        cli->head.type = MQTT_TYPE_DISCONNECT;
        cli->cb(cli, cli->head.type);
    }
    // reconnect
    if (cli->reconn_setting && reconn_setting_can_retry(cli->reconn_setting)) {
        uint32_t delay = reconn_setting_calc_delay(cli->reconn_setting);
        cli->reconn_timer = htimer_add(cli->loop, reconnect_timer_cb, delay, 1);
        hevent_set_userdata(cli->reconn_timer, cli);
    }
}

static void on_packet(hio_t* io, void* buf, int len) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(io);
    unsigned char* p = (unsigned char*)buf;
    unsigned char* end = p + len;
    memset(&cli->head, 0, sizeof(mqtt_head_t));
    int headlen = mqtt_head_unpack(&cli->head, p, len);
    if (headlen <= 0) return;
    p += headlen;
    switch (cli->head.type) {
    // case MQTT_TYPE_CONNECT:
    case MQTT_TYPE_CONNACK:
    {
        if (cli->head.length < 2) {
            hloge("MQTT CONNACK malformed!");
            hio_close(io);
            return;
        }
        unsigned char conn_flags = 0, rc = 0;
        POP8(p, conn_flags);
        POP8(p, rc);
        if (rc != MQTT_CONNACK_ACCEPTED) {
            cli->error = rc;
            hloge("MQTT CONNACK error=%d", cli->error);
            hio_close(io);
            return;
        }
        cli->connected = 1;
        if (cli->keepalive) {
            hio_set_heartbeat(io, cli->keepalive * 1000, mqtt_send_ping);
        }
    }
        break;
    case MQTT_TYPE_PUBLISH:
    {
        if (cli->head.length < 2) {
            hloge("MQTT PUBLISH malformed!");
            hio_close(io);
            return;
        }
        memset(&cli->message, 0, sizeof(mqtt_message_t));
        POP16(p, cli->message.topic_len);
        if (end - p < cli->message.topic_len) {
            hloge("MQTT PUBLISH malformed!");
            hio_close(io);
            return;
        }
        // NOTE: Not deep copy
        cli->message.topic = (char*)p;
        p += cli->message.topic_len;
        if (cli->head.qos > 0) {
            if (end - p < 2) {
                hloge("MQTT PUBLISH malformed!");
                hio_close(io);
                return;
            }
            POP16(p, cli->mid);
        }
        cli->message.payload_len = end - p;
        if (cli->message.payload_len > 0) {
            // NOTE: Not deep copy
            cli->message.payload = (char*)p;
        }
        cli->message.qos = cli->head.qos;
        if (cli->message.qos == 0) {
            // Do nothing
        } else if (cli->message.qos == 1) {
            mqtt_send_head_with_mid(io, MQTT_TYPE_PUBACK, cli->mid);
        } else if (cli->message.qos == 2) {
            mqtt_send_head_with_mid(io, MQTT_TYPE_PUBREC, cli->mid);
        }
    }
        break;
    case MQTT_TYPE_PUBACK:
    case MQTT_TYPE_PUBREC:
    case MQTT_TYPE_PUBREL:
    case MQTT_TYPE_PUBCOMP:
    {
        if (cli->head.length < 2) {
            hloge("MQTT PUBACK malformed!");
            hio_close(io);
            return;
        }
        POP16(p, cli->mid);
        if (cli->head.type == MQTT_TYPE_PUBREC) {
            mqtt_send_head_with_mid(io, MQTT_TYPE_PUBREL, cli->mid);
        } else if (cli->head.type == MQTT_TYPE_PUBREL) {
            mqtt_send_head_with_mid(io, MQTT_TYPE_PUBCOMP, cli->mid);
        }
    }
        break;
    // case MQTT_TYPE_SUBSCRIBE:
    //     break;
    case MQTT_TYPE_SUBACK:
    {
        if (cli->head.length < 2) {
            hloge("MQTT SUBACK malformed!");
            hio_close(io);
            return;
        }
        POP16(p, cli->mid);
    }
        break;
    // case MQTT_TYPE_UNSUBSCRIBE:
    //     break;
    case MQTT_TYPE_UNSUBACK:
    {
        if (cli->head.length < 2) {
            hloge("MQTT UNSUBACK malformed!");
            hio_close(io);
            return;
        }
        POP16(p, cli->mid);
    }
        break;
    case MQTT_TYPE_PINGREQ:
        // printf("recv ping\n");
        // printf("send pong\n");
        mqtt_send_pong(io);
        return;
    case MQTT_TYPE_PINGRESP:
        // printf("recv pong\n");
        cli->ping_cnt = 0;
        return;
    case MQTT_TYPE_DISCONNECT:
        hio_close(io);
        return;
    default:
        hloge("MQTT client received wrong type=%d", (int)cli->head.type);
        hio_close(io);
        return;
    }

    if (cli->cb) {
        cli->cb(cli, cli->head.type);
    }
}

static void on_connect(hio_t* io) {
    mqtt_client_t* cli = (mqtt_client_t*)hevent_userdata(io);
    if (cli->cb) {
        cli->head.type = MQTT_TYPE_CONNECT;
        cli->cb(cli, cli->head.type);
    }
    if (cli->reconn_setting) {
        reconn_setting_reset(cli->reconn_setting);
    }

    static unpack_setting_t mqtt_unpack_setting;
    mqtt_unpack_setting.mode = UNPACK_BY_LENGTH_FIELD;
    mqtt_unpack_setting.package_max_length = DEFAULT_MQTT_PACKAGE_MAX_LENGTH;
    mqtt_unpack_setting.body_offset = 2;
    mqtt_unpack_setting.length_field_offset = 1;
    mqtt_unpack_setting.length_field_bytes = 1;
    mqtt_unpack_setting.length_field_coding = ENCODE_BY_VARINT;
    hio_set_unpack(io, &mqtt_unpack_setting);

    // start recv packet
    hio_setcb_read(io, on_packet);
    hio_read(io);

    mqtt_client_login(cli);
}

mqtt_client_t* mqtt_client_new(hloop_t* loop) {
    if (loop == NULL) {
        loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
        if (loop == NULL) return NULL;
    }
    mqtt_client_t* cli = NULL;
    HV_ALLOC_SIZEOF(cli);
    if (cli == NULL) return NULL;
    cli->loop = loop;
    cli->protocol_version = MQTT_PROTOCOL_V311;
    cli->keepalive = DEFAULT_MQTT_KEEPALIVE;
    hmutex_init(&cli->mutex_);
    return cli;
}

void mqtt_client_free(mqtt_client_t* cli) {
    if (!cli) return;
    hmutex_destroy(&cli->mutex_);
    if (cli->reconn_timer) {
        htimer_del(cli->reconn_timer);
        cli->reconn_timer = NULL;
    }
    if (cli->ssl_ctx && cli->alloced_ssl_ctx) {
        hssl_ctx_free(cli->ssl_ctx);
        cli->ssl_ctx = NULL;
    }
    HV_FREE(cli->reconn_setting);
    HV_FREE(cli->will);
    HV_FREE(cli);
}

void mqtt_client_run (mqtt_client_t* cli) {
    if (!cli || !cli->loop) return;
    hloop_run(cli->loop);
}

void mqtt_client_stop(mqtt_client_t* cli) {
    if (!cli || !cli->loop) return;
    hloop_stop(cli->loop);
}

void mqtt_client_set_id(mqtt_client_t* cli, const char* id) {
    if (!cli || !id) return;
    hv_strncpy(cli->client_id, id, sizeof(cli->client_id));
}

void mqtt_client_set_will(mqtt_client_t* cli, mqtt_message_t* will) {
    if (!cli || !will) return;
    if (cli->will == NULL) {
        HV_ALLOC_SIZEOF(cli->will);
    }
    memcpy(cli->will, will, sizeof(mqtt_message_t));
}

void mqtt_client_set_auth(mqtt_client_t* cli, const char* username, const char* password) {
    if (!cli) return;
    if (username) {
        hv_strncpy(cli->username, username, sizeof(cli->username));
    }
    if (password) {
        hv_strncpy(cli->password, password, sizeof(cli->password));
    }
}

void mqtt_client_set_callback(mqtt_client_t* cli, mqtt_client_cb cb) {
    if (!cli) return;
    cli->cb = cb;
}

void  mqtt_client_set_userdata(mqtt_client_t* cli, void* userdata) {
    if (!cli) return;
    cli->userdata = userdata;
}

void* mqtt_client_get_userdata(mqtt_client_t* cli) {
    if (!cli) return NULL;
    return cli->userdata;
}

int mqtt_client_get_last_error(mqtt_client_t* cli) {
    if (!cli) return -1;
    return cli->error;
}

int mqtt_client_set_ssl_ctx(mqtt_client_t* cli, hssl_ctx_t ssl_ctx) {
    cli->ssl_ctx = ssl_ctx;
    return 0;
}

int mqtt_client_new_ssl_ctx(mqtt_client_t* cli, hssl_ctx_opt_t* opt) {
    opt->endpoint = HSSL_CLIENT;
    hssl_ctx_t ssl_ctx = hssl_ctx_new(opt);
    if (ssl_ctx == NULL) return ERR_NEW_SSL_CTX;
    cli->alloced_ssl_ctx = true;
    return mqtt_client_set_ssl_ctx(cli, ssl_ctx);
}

int mqtt_client_set_reconnect(mqtt_client_t* cli, reconn_setting_t* reconn) {
    if (reconn == NULL) {
        HV_FREE(cli->reconn_setting);
        return 0;
    }
    if (cli->reconn_setting == NULL) {
        HV_ALLOC_SIZEOF(cli->reconn_setting);
    }
    *cli->reconn_setting = *reconn;
    return 0;
}

int mqtt_client_reconnect(mqtt_client_t* cli) {
    mqtt_client_connect(cli, cli->host, cli->port, cli->ssl);
    return 0;
}

void mqtt_client_set_connect_timeout(mqtt_client_t* cli, int ms) {
    cli->connect_timeout = ms;
}

int mqtt_client_connect(mqtt_client_t* cli, const char* host, int port, int ssl) {
    if (!cli) return -1;
    hv_strncpy(cli->host, host, sizeof(cli->host));
    cli->port = port;
    cli->ssl = ssl;
    hio_t* io = hio_create_socket(cli->loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) return -1;
    if (ssl) {
        if (cli->ssl_ctx) {
            hio_set_ssl_ctx(io, cli->ssl_ctx);
        }
        hio_enable_ssl(io);
    }
    if (cli->connect_timeout > 0) {
        hio_set_connect_timeout(io, cli->connect_timeout);
    }
    cli->io = io;
    hevent_set_userdata(io, cli);
    hio_setcb_connect(io, on_connect);
    hio_setcb_close(io, on_close);
    return hio_connect(io);
}

bool mqtt_client_is_connected(mqtt_client_t* cli) {
    return cli && cli->connected;
}

int mqtt_client_disconnect(mqtt_client_t* cli) {
    if (!cli || !cli->io) return -1;
    // cancel reconnect first
    mqtt_client_set_reconnect(cli, NULL);
    mqtt_send_disconnect(cli->io);
    return hio_close(cli->io);
}

int mqtt_client_publish(mqtt_client_t* cli, mqtt_message_t* msg) {
    if (!cli || !cli->io || !msg) return -1;
    if (!cli->connected) return -2;
    int topic_len = msg->topic_len ? msg->topic_len : strlen(msg->topic);
    int payload_len = msg->payload_len ? msg->payload_len : strlen(msg->payload);
    int len = 2 + topic_len + payload_len;
    if (msg->qos > 0) len += 2; // mid
    unsigned short mid = 0;

    mqtt_head_t head;
    memset(&head, 0, sizeof(head));
    head.type = MQTT_TYPE_PUBLISH;
    head.qos = msg->qos & 3;
    head.retain = msg->retain;
    head.length = len;
    int buflen = mqtt_estimate_length(&head);
    // NOTE: send payload alone
    buflen -= payload_len;
    unsigned char* buf = NULL;
    HV_STACK_ALLOC(buf, buflen);
    unsigned char* p = buf;
    int headlen = mqtt_head_pack(&head, p);
    p += headlen;
    PUSH16(p, topic_len);
    PUSH_N(p, msg->topic, topic_len);
    if (msg->qos) {
        mid = mqtt_next_mid();
        PUSH16(p, mid);
    }

    hmutex_lock(&cli->mutex_);
    // send head + topic + mid
    int nwrite = hio_write(cli->io, buf, p - buf);
    HV_STACK_FREE(buf);
    if (nwrite < 0) {
        goto unlock;
    }

    // send payload
    nwrite = hio_write(cli->io, msg->payload, payload_len);

unlock:
    hmutex_unlock(&cli->mutex_);
    return nwrite < 0 ? nwrite : mid;
}

int mqtt_client_subscribe(mqtt_client_t* cli, const char* topic, int qos) {
    if (!cli || !cli->io || !topic) return -1;
    if (!cli->connected) return -2;
    int topic_len = strlen(topic);
    int len = 2 + 2 + topic_len + 1;

    mqtt_head_t head;
    memset(&head, 0, sizeof(head));
    head.type = MQTT_TYPE_SUBSCRIBE;
    head.qos = 1;
    head.length = len;
    int buflen = mqtt_estimate_length(&head);
    unsigned char* buf = NULL;
    HV_STACK_ALLOC(buf, buflen);
    unsigned char* p = buf;
    int headlen = mqtt_head_pack(&head, p);
    p += headlen;
    unsigned short mid = mqtt_next_mid();
    PUSH16(p, mid);
    PUSH16(p, topic_len);
    PUSH_N(p, topic, topic_len);
    PUSH8(p, qos & 3);
    // send head + mid + topic + qos
    int nwrite = mqtt_client_send(cli, buf, p - buf);
    HV_STACK_FREE(buf);
    return nwrite < 0 ? nwrite : mid;
}

int mqtt_client_unsubscribe(mqtt_client_t* cli, const char* topic) {
    if (!cli || !cli->io || !topic) return -1;
    if (!cli->connected) return -2;
    int topic_len = strlen(topic);
    int len = 2 + 2 + topic_len;

    mqtt_head_t head;
    memset(&head, 0, sizeof(head));
    head.type = MQTT_TYPE_UNSUBSCRIBE;
    head.qos = 1;
    head.length = len;
    int buflen = mqtt_estimate_length(&head);
    unsigned char* buf = NULL;
    HV_STACK_ALLOC(buf, buflen);
    unsigned char* p = buf;
    int headlen = mqtt_head_pack(&head, p);
    p += headlen;
    unsigned short mid = mqtt_next_mid();
    PUSH16(p, mid);
    PUSH16(p, topic_len);
    PUSH_N(p, topic, topic_len);
    // send head + mid + topic
    int nwrite = mqtt_client_send(cli, buf, p - buf);
    HV_STACK_FREE(buf);
    return nwrite < 0 ? nwrite : mid;
}
