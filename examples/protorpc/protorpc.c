#include "protorpc.h"

#include <string.h> // import memcpy

int protorpc_pack(const protorpc_message* msg, void* buf, int len) {
    if (!msg || !buf || !len) return -1;
    const protorpc_head* head = &(msg->head);
    unsigned int packlen = protorpc_package_length(head);
    // Check is buffer enough
    if (len < packlen) {
        return -2;
    }
    unsigned char* p = (unsigned char*)buf;
    // flags
    *p++ = head->flags;
    // hton length
    unsigned int length = head->length;
    *p++ = (length >> 24) & 0xFF;
    *p++ = (length >> 16) & 0xFF;
    *p++ = (length >>  8) & 0xFF;
    *p++ =  length        & 0xFF;
    // memcpy body
    if (msg->body && head->length) {
        memcpy(p, msg->body, head->length);
    }

    return packlen;
}

int protorpc_unpack(protorpc_message* msg, const void* buf, int len) {
    if (!msg || !buf || !len) return -1;
    if (len < PROTORPC_HEAD_LENGTH) return -2;
    protorpc_head* head = &(msg->head);
    const unsigned char* p = (const unsigned char*)buf;
    // flags
    head->flags = *p++;
    // ntoh length
    head->length  = ((unsigned int)*p++) << 24;
    head->length |= ((unsigned int)*p++) << 16;
    head->length |= ((unsigned int)*p++) << 8;
    head->length |= *p++;
    // Check is buffer enough
    unsigned int packlen = protorpc_package_length(head);
    if (len < packlen) {
        return -3;
    }
    // NOTE: just shadow copy
    if (len > PROTORPC_HEAD_LENGTH) {
        msg->body = (const char*)buf + PROTORPC_HEAD_LENGTH;
    }

    return packlen;
}
