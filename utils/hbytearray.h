#ifndef HV_BYTE_ARRAY_H_
#define HV_BYTE_ARRAY_H_

#include "hbuf.h"
#include "base64.h"

class HByteArray : public HVLBuf {
public:
    HByteArray() : HVLBuf() {}
    HByteArray(int cap) : HVLBuf(cap) {}
    HByteArray(void* data, int len) : HVLBuf(data, len) {}

    bool encodeBase64(void* ptr, int len) {
        int base64_len = BASE64_ENCODE_OUT_SIZE(len) + 1;   // +1 for '\0'
        resize(base64_len);
        return base64_encode((unsigned char*)ptr, len, (char*)data()) == BASE64_OK;
    }

    bool decodeBase64(const char* base64) {
        int out_len = BASE64_DECODE_OUT_SIZE(strlen(base64));
        resize(out_len);
        return base64_decode(base64, strlen(base64), (unsigned char*)data()) == BASE64_OK;
    }
};

#endif  // HV_BYTE_ARRAY_H_
