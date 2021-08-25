#include "jsonrpc.h"

#include <string.h> // import memcpy

int jsonrpc_pack(const jsonrpc_message* msg, void* buf, int len) {
    if (!msg || !buf || !len) return -1;
    const jsonrpc_head* head = &(msg->head);
    unsigned int packlen = jsonrpc_package_length(head);
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
    memcpy(p, msg->body, head->length);

    return packlen;
}

int jsonrpc_unpack(jsonrpc_message* msg, const void* buf, int len) {
    if (!msg || !buf || !len) return -1;
    if (len < JSONRPC_HEAD_LENGTH) return -2;
    jsonrpc_head* head = &(msg->head);
    const unsigned char* p = (const unsigned char*)buf;
    // flags
    head->flags = *p++;
    // ntoh length
    head->length  = ((unsigned int)*p++) << 24;
    head->length |= ((unsigned int)*p++) << 16;
    head->length |= ((unsigned int)*p++) << 8;
    head->length |= *p++;
    // Check is buffer enough
    unsigned int packlen = jsonrpc_package_length(head);
    if (len < packlen) {
        return -3;
    }
    // NOTE: just shadow copy
    msg->body = (const char*)buf + JSONRPC_HEAD_LENGTH;

    return packlen;
}
