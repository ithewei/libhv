#include "wsdef.h"

#include <string.h>

#include "sha1.h"
#include "base64.h"

#include "websocket_parser.h"

// base64_encode( SHA1(key + magic) )
void ws_encode_key(const char* key, char accept[]) {
    char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[20] = {0};
    HV_SHA1_CTX ctx;
    HV_SHA1Init(&ctx);
    HV_SHA1Update(&ctx, (unsigned char*)key, (uint32_t)strlen(key));
    HV_SHA1Update(&ctx, (unsigned char*)magic, (uint32_t)strlen(magic));
    HV_SHA1Final(digest, &ctx);
    hv_base64_encode(digest, 20, accept);
}

// fix-header[2] + var-length[2/8] + mask[4] + data[data_len]
int ws_calc_frame_size(int data_len, bool has_mask) {
    int size = data_len + 2;
    if (data_len >=126) {
        if (data_len > 0xFFFF) {
            size += 8;
        } else {
            size += 2;
        }
    }
    if (has_mask) size += 4;
    return size;
}

int ws_build_frame(
    char* out,
    const char* data, int data_len,
    const char mask[4], bool has_mask,
    enum ws_opcode opcode,
    bool fin) {
    int flags = opcode;
    if (fin) flags |= WS_FIN;
    if (has_mask) flags |=  WS_HAS_MASK;
    return (int)websocket_build_frame(out, (websocket_flags)flags, mask, data, data_len);
}
