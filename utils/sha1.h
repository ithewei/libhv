#ifndef HV_SHA1_H_
#define HV_SHA1_H_

/*
   SHA-1 in C
   By Steve Reid <steve@edmweb.com>
   100% Public Domain
 */

/* for uint32_t */
#include <stdint.h>

#include "hexport.h"

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[64];
} SHA1_CTX;

BEGIN_EXTERN_C

void SHA1Transform(
    uint32_t state[5],
    const unsigned char buffer[64]
    );

void SHA1Init(
    SHA1_CTX * context
    );

void SHA1Update(
    SHA1_CTX * context,
    const unsigned char *data,
    uint32_t len
    );

void SHA1Final(
    unsigned char digest[20],
    SHA1_CTX * context
    );

void SHA1(
    char *hash_out,
    const char *str,
    int len);

HV_EXPORT void hv_sha1(unsigned char* input, uint32_t inputlen, unsigned char digest[20]);

// NOTE: if outputlen > 40: output[40] = '\0'
HV_EXPORT void hv_sha1_hex(unsigned char* input, uint32_t inputlen, char* output, uint32_t outputlen);

END_EXTERN_C

#endif // HV_SHA1_H_
