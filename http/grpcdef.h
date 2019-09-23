#ifndef GRPC_DEF_H_
#define GRPC_DEF_H_

#ifdef __cplusplus
extern "C" {
#endif

// Length-Prefixed-Message

// flags:1byte + length:4bytes = 5bytes
#define GRPC_MESSAGE_HDLEN  5

typedef struct {
    unsigned char   flags;
    int             length;
} grpc_message_hd;

typedef struct {
    unsigned char   flags;
    int             length;
    unsigned char*  message;
} grpc_message;

static inline void grpc_message_hd_pack(const grpc_message_hd* hd, unsigned char* buf) {
    // hton
    int length = hd->length;
    unsigned char* p = buf;
    *p++ = hd->flags;
    *p++ = (length >> 24) & 0xFF;
    *p++ = (length >> 16) & 0xFF;
    *p++ = (length >>  8) & 0xFF;
    *p++ =  length        & 0xFF;
}

static inline void grpc_message_hd_unpack(const unsigned char* buf, grpc_message_hd* hd) {
    // ntoh
    const unsigned char* p = buf;
    hd->flags = *p++;
    hd->length  = *p++ << 24;
    hd->length += *p++ << 16;
    hd->length += *p++ << 8;
    hd->length += *p++;
}

#ifdef __cplusplus
}
#endif

#endif // GRPC_DEF_H_
