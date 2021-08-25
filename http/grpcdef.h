#ifndef HV_GRPC_DEF_H_
#define HV_GRPC_DEF_H_

#ifdef __cplusplus
extern "C" {
#endif

// Length-Prefixed-Message

// flags:1byte + length:4bytes = 5bytes
#define GRPC_MESSAGE_HDLEN  5

typedef struct {
    unsigned char   flags;
    unsigned int    length;
} grpc_message_hd;

typedef struct {
    unsigned char   flags;
    unsigned int    length;
    unsigned char*  message;
} grpc_message;

static inline void grpc_message_hd_pack(const grpc_message_hd* hd, unsigned char* buf) {
    unsigned char* p = buf;
    // flags
    *p++ = hd->flags;
    // hton length
    unsigned int length = hd->length;
    *p++ = (length >> 24) & 0xFF;
    *p++ = (length >> 16) & 0xFF;
    *p++ = (length >>  8) & 0xFF;
    *p++ =  length        & 0xFF;
}

static inline void grpc_message_hd_unpack(grpc_message_hd* hd, const unsigned char* buf) {
    const unsigned char* p = buf;
    // flags
    hd->flags = *p++;
    // ntoh length
    hd->length  = ((unsigned int)*p++) << 24;
    hd->length |= ((unsigned int)*p++) << 16;
    hd->length |= ((unsigned int)*p++) << 8;
    hd->length |= *p++;
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

#ifdef __cplusplus
}
#endif

#endif // HV_GRPC_DEF_H_
