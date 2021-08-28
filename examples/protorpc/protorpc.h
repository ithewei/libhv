#ifndef HV_PROTO_RPC_H_
#define HV_PROTO_RPC_H_

#ifdef __cplusplus
extern "C" {
#endif

// flags:1byte + length:4bytes = 5bytes
#define PROTORPC_HEAD_LENGTH  5
typedef struct {
    unsigned char   flags;
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

// @retval >0 package_length, <0 error
int protorpc_pack(const protorpc_message* msg, void* buf, int len);
// @retval >0 package_length, <0 error
int protorpc_unpack(protorpc_message* msg, const void* buf, int len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HV_PROTO_RPC_H_
