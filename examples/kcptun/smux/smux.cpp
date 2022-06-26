#include "smux.h"

#define SMUX_USE_LITTLE_ENDIAN 1

int smux_frame_pack(const smux_frame_t* frame, void* buf, int len) {
    if (!frame || !buf || !len) return -1;
    const smux_head_t* head = &(frame->head);
    unsigned int packlen = smux_package_length(head);
    // Check is buffer enough
    if (len < packlen) {
        return -2;
    }
    unsigned char* p = (unsigned char*)buf;
    *p++ = head->version;
    *p++ = head->cmd;
#if SMUX_USE_LITTLE_ENDIAN
    *p++ =  head->length;
    *p++ = (head->length >> 8) & 0xFF;
#else
    // hton length
    *p++ = (head->length >> 8) & 0xFF;
    *p++ =  head->length;
#endif

    uint32_t sid = head->sid;
#if SMUX_USE_LITTLE_ENDIAN
    *p++ =  sid        & 0xFF;
    *p++ = (sid >>  8) & 0xFF;
    *p++ = (sid >> 16) & 0xFF;
    *p++ = (sid >> 24) & 0xFF;
#else
    // hton sid
    *p++ = (sid >> 24) & 0xFF;
    *p++ = (sid >> 16) & 0xFF;
    *p++ = (sid >>  8) & 0xFF;
    *p++ =  sid        & 0xFF;
#endif
    // memcpy data
    if (frame->data && head->length) {
        memcpy(p, frame->data, frame->head.length);
    }
    return packlen;
}

int smux_frame_unpack(smux_frame_t* frame, const void* buf, int len) {
    if (!frame || !buf || !len) return -1;
    if (len < SMUX_HEAD_LENGTH) return -2;
    smux_head_t* head = &(frame->head);
    unsigned char* p = (unsigned char*)buf;
    head->version = *p++;
    head->cmd = *p++;
#if SMUX_USE_LITTLE_ENDIAN
    head->length  = *p++;
    head->length |= ((uint16_t)*p++) << 8;
#else
    // ntoh length
    head->length  = ((uint16_t)*p++) << 8;
    head->length |= *p++;
#endif

#if SMUX_USE_LITTLE_ENDIAN
    head->sid  = *p++;
    head->sid |= ((uint32_t)*p++) << 8;
    head->sid |= ((uint32_t)*p++) << 16;
    head->sid |= ((uint32_t)*p++) << 24;
#else
    // ntoh sid
    head->sid  = ((uint32_t)*p++) << 24;
    head->sid |= ((uint32_t)*p++) << 16;
    head->sid |= ((uint32_t)*p++) << 8;
    head->sid |= *p++;
#endif
    // NOTE: just shadow copy
    if (len > SMUX_HEAD_LENGTH) {
        frame->data = (const char*)buf + SMUX_HEAD_LENGTH;
    }
    unsigned int packlen = smux_package_length(head);
    return MIN(len, packlen);
}
