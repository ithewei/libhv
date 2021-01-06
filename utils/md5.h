#ifndef HV_MD5_H_
#define HV_MD5_H_

#include "hexport.h"

typedef struct {
    unsigned int    count[2];
    unsigned int    state[4];
    unsigned char   buffer[64];
} MD5_CTX;

BEGIN_EXTERN_C

void MD5Init(MD5_CTX *ctx);
void MD5Update(MD5_CTX *ctx, unsigned char *input, unsigned int inputlen);
void MD5Final(MD5_CTX *ctx, unsigned char digest[16]);

HV_EXPORT void hv_md5(unsigned char* input, unsigned int inputlen, unsigned char digest[16]);

// NOTE: if outputlen > 32: output[32] = '\0'
HV_EXPORT void hv_md5_hex(unsigned char* input, unsigned int inputlen, char* output, unsigned int outputlen);

END_EXTERN_C

#endif // HV_MD5_H_
