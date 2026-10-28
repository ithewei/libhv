/*
 * @build: gcc -o bin/mqtt_protocol_test unittest/mqtt_protocol_test.c mqtt/mqtt_protocol.c -I. -Ibase -Imqtt
 *
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mqtt_protocol.h"

static void test_head_unpack() {
    mqtt_head_t head;
    // PUBLISH qos=1 retain=1, remaining length 3
    const unsigned char publish[] = { 0x33, 0x03, 0x00, 0x01, 0x74 };
    memset(&head, 0, sizeof(head));
    assert(mqtt_head_unpack(&head, publish, sizeof(publish)) == 2);
    assert(head.type == MQTT_TYPE_PUBLISH);
    assert(head.qos == 1);
    assert(head.retain == 1);
    assert(head.length == 3);

    // Remaining Length is at most 4 bytes
    const unsigned char four[] = { 0x30, 0xFF, 0xFF, 0xFF, 0x7F };
    memset(&head, 0, sizeof(head));
    assert(mqtt_head_unpack(&head, four, sizeof(four)) == 5);
    assert(head.length == 268435455);

    const unsigned char five[] = { 0x30, 0x83, 0x80, 0x80, 0x80, 0x00, 0x00, 0x01, 0x74 };
    memset(&head, 0, sizeof(head));
    assert(mqtt_head_unpack(&head, five, sizeof(five)) < 0);
}

static void test_head_flags_valid() {
    mqtt_head_t head;
    int type, qos, dup, retain;
    for (type = MQTT_TYPE_CONNECT; type <= MQTT_TYPE_AUTH; ++type) {
        for (dup = 0; dup <= 1; ++dup) {
            for (qos = 0; qos <= 3; ++qos) {
                for (retain = 0; retain <= 1; ++retain) {
                    memset(&head, 0, sizeof(head));
                    head.type = type;
                    head.dup = dup;
                    head.qos = qos;
                    head.retain = retain;
                    int flags = (dup << 3) | (qos << 1) | retain;
                    int expected;
                    if (type == MQTT_TYPE_PUBLISH) {
                        expected = qos != 3;
                    } else if (type == MQTT_TYPE_PUBREL ||
                               type == MQTT_TYPE_SUBSCRIBE ||
                               type == MQTT_TYPE_UNSUBSCRIBE) {
                        expected = flags == 0x2;
                    } else {
                        expected = flags == 0;
                    }
                    assert(mqtt_head_flags_valid(&head) == expected);
                }
            }
        }
    }
}

int main() {
    test_head_unpack();
    test_head_flags_valid();
    printf("mqtt_protocol_test OK\n");
    return 0;
}
