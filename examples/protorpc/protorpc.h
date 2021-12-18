#ifndef HV_PROTO_RPC_H_
#define HV_PROTO_RPC_H_

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTORPC_NAME       "HRPC"
#define PROTORPC_VERSION    1

// protocol:4bytes + version:1byte + flags:1byte + reserved:2bytes + length:4bytes = 12bytes
#define PROTORPC_HEAD_LENGTH                12
#define PROTORPC_HEAD_LENGTH_FIELD_OFFSET   8
#define PROTORPC_HEAD_LENGTH_FIELD_BYTES    4
typedef struct {
    unsigned char   protocol[4];
    unsigned char   version;
    unsigned char   flags;
    unsigned char   reserved[2];
    unsigned int    length;
} protorpc_head;

typedef const char* protorpc_body;

typedef struct {
    protorpc_head   head;
    protorpc_body   body;
} protorpc_message;

static inline unsigned int protorpc_package_length(const protorpc_head* head) {
    return PROTORPC_HEAD_LENGTH + head->length;
}

static inline void protorpc_head_init(protorpc_head* head) {
    // protocol = HRPC
    memcpy(head->protocol, PROTORPC_NAME, 4);
    head->version = PROTORPC_VERSION;
    head->reserved[0] = head->reserved[1] = 0;
    head->length = 0;
}

static inline void protorpc_message_init(protorpc_message* msg) {
    protorpc_head_init(&msg->head);
    msg->body = NULL;
}

static inline int protorpc_head_check(protorpc_head* head) {
    if (memcmp(head->protocol, PROTORPC_NAME, 4) != 0) {
        return -1;
    }
    if (head->version != PROTORPC_VERSION) {
        return -2;
    }
    return 0;
}

// @retval >0 package_length, <0 error
int protorpc_pack(const protorpc_message* msg, void* buf, int len);
// @retval >0 package_length, <0 error
int protorpc_unpack(protorpc_message* msg, const void* buf, int len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HV_PROTO_RPC_H_
