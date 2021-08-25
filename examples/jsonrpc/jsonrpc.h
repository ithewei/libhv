#ifndef HV_JSON_RPC_H_
#define HV_JSON_RPC_H_

// flags:1byte + length:4bytes = 5bytes
#define JSONRPC_HEAD_LENGTH  5
typedef struct {
    unsigned char   flags;
    unsigned int    length;
} jsonrpc_head;

typedef const char* jsonrpc_body;

typedef struct {
    jsonrpc_head   head;
    jsonrpc_body   body;
} jsonrpc_message;

static inline unsigned int jsonrpc_package_length(const jsonrpc_head* head) {
    return JSONRPC_HEAD_LENGTH + head->length;
}

// @retval >0 package_length, <0 error
int jsonrpc_pack(const jsonrpc_message* msg, void* buf, int len);
// @retval >0 package_length, <0 error
int jsonrpc_unpack(jsonrpc_message* msg, const void* buf, int len);

#endif // HV_JSON_RPC_H_
