#ifndef HV_BASE64_H_
#define HV_BASE64_H_

#include "hexport.h"

enum {BASE64_OK = 0, BASE64_INVALID};

#define BASE64_ENCODE_OUT_SIZE(s)   (((s) + 2) / 3 * 4)
#define BASE64_DECODE_OUT_SIZE(s)   (((s)) / 4 * 3)

BEGIN_EXTERN_C

HV_EXPORT int base64_encode(const unsigned char *in, unsigned int inlen, char *out);
HV_EXPORT int base64_decode(const char *in, unsigned int inlen, unsigned char *out);

END_EXTERN_C

#endif // HV_BASE64_H_
