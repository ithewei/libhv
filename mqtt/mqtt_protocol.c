#include "mqtt_protocol.h"
#include "hmath.h"

int mqtt_head_pack(mqtt_head_t* head, unsigned char buf[]) {
    buf[0] = (head->type << 4) |
             (head->dup  << 3) |
             (head->qos  << 1) |
             (head->retain);
    int bytes = varint_encode(head->length, buf + 1);
    return 1 + bytes;
}

int mqtt_head_unpack(mqtt_head_t* head, const unsigned char* buf, int len) {
    head->type   = (buf[0] >> 4) & 0x0F;
    head->dup    = (buf[0] >> 3) & 0x01;
    head->qos    = (buf[0] >> 1) & 0x03;
    head->retain =  buf[0] & 0x01;
    int bytes = len - 1;
    head->length = varint_decode(buf + 1, &bytes);
    if (bytes <= 0) return bytes;
    return 1 + bytes;
}
