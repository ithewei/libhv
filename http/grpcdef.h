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

// protobuf
// tag = field_num << 3 | wire_type
// varint(tag) [+ varint(length_delimited)] + value;
typedef enum {
    WIRE_TYPE_VARINT           = 0,
    WIRE_TYPE_FIXED64          = 1,
    WIRE_TYPE_LENGTH_DELIMITED = 2,
    WIRE_TYPE_START_GROUP      = 3,
    WIRE_TYPE_END_GROUP        = 4,
    WIRE_TYPE_FIXED32          = 5,
} wire_type;

typedef enum {
    FIELD_TYPE_DOUBLE         = 1,
    FIELD_TYPE_FLOAT          = 2,
    FIELD_TYPE_INT64          = 3,
    FIELD_TYPE_UINT64         = 4,
    FIELD_TYPE_INT32          = 5,
    FIELD_TYPE_FIXED64        = 6,
    FIELD_TYPE_FIXED32        = 7,
    FIELD_TYPE_BOOL           = 8,
    FIELD_TYPE_STRING         = 9,
    FIELD_TYPE_GROUP          = 10,
    FIELD_TYPE_MESSAGE        = 11,
    FIELD_TYPE_BYTES          = 12,
    FIELD_TYPE_UINT32         = 13,
    FIELD_TYPE_ENUM           = 14,
    FIELD_TYPE_SFIXED32       = 15,
    FIELD_TYPE_SFIXED64       = 16,
    FIELD_TYPE_SINT32         = 17,
    FIELD_TYPE_SINT64         = 18,
    MAX_FIELD_TYPE            = 18,
} field_type;

#define PROTOBUF_MAKE_TAG(field_number, wire_type)  ((field_number) << 3 | (wire_type))
#define PROTOBUF_FILED_NUMBER(tag)                  ((tag) >> 3)
#define PROTOBUF_WIRE_TYPE(tag)                     ((tag) & 0x07)

// varint little-endian
// MSB
static inline int varint_encode(long long value, unsigned char* buf) {
    unsigned char ch;
    unsigned char *p = buf;
    int bytes = 0;
    do {
        ch = value & 0x7F;
        value >>= 7;
        *p++ = value == 0 ? ch : (ch | 0x80);
        ++bytes;
    } while (value);
    return bytes;
}

// @param[IN|OUT] len: in=>buflen, out=>varint bytesize
static inline long long varint_decode(const unsigned char* buf, int* len) {
    long long ret = 0;
    int bytes = 0;
    int bits = 0;
    const unsigned char *p = buf;
    unsigned char ch;
    do {
        if (len && *len && bytes == *len) {
            break;
        }
        ch = *p & 0x7F;
        ret |= (ch << bits);
        bits += 7;
        ++bytes;
        if (!(*p & 0x80)) break;
        ++p;
    } while(bytes < 10);
    *len = bytes;
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif // GRPC_DEF_H_
