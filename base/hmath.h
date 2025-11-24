#ifndef HV_MATH_H_
#define HV_MATH_H_

#include <math.h>

static inline unsigned long floor2e(unsigned long num) {
    unsigned long n = num;
    int e = 0;
    while (n>>=1) ++e;
    unsigned long ret = 1;
    while (e--) ret<<=1;
    return ret;
}

static inline unsigned long ceil2e(unsigned long num) {
    // 2**0 = 1
    if (num == 0 || num == 1)   return 1;
    unsigned long n = num - 1;
    int e = 1;
    while (n>>=1) ++e;
    unsigned long ret = 1;
    while (e--) ret<<=1;
    return ret;
}

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
    int bytes = 0, bits = 0;
    const unsigned char *p = buf;
    do {
        if (len && *len && bytes == *len) {
            // Not enough length
            *len = 0;
            return 0;
        }
        ret |= ((long long)(*p & 0x7F)) << bits;
        ++bytes;
        if ((*p & 0x80) == 0) {
            // Found end
            if (len) *len = bytes;
            return ret;
        }
        ++p;
        bits += 7;
    } while(bytes < 10);

    // Not found end
    if (len) *len = -1;
    return ret;
}

static inline int asn1_encode(long long value, unsigned char* buf) {
    /*
     * unrolled for efficiency
     * check each possibitlity of the 4 byte integer
     */
    unsigned char* p = buf;
    if (value < 128)
    {
        *p = (unsigned char)value;
        return 1;
    }
    else if (value < 256)
    {
        *p = 0x81;
        p++;
        *p = (unsigned char)value;
        return 2;
    }
    else if (value < 65536)
    {
        *p = 0x82;
        p++;
        *p = (unsigned char)(value>>8);
        p++;
        *p = (unsigned char)value;
        return 3;
    }
    else if (value < 16777126)
    {
        *p = 0x83;
        p++;
        *p = (unsigned char)(value>>16);
        p++;
        *p = (unsigned char)(value >> 8);
        p++;
        *p = (unsigned char)value;
        return 4;
    }
    else
    {
        *p = 0x84;
        p++;
        *p = (unsigned char)(value >> 24);
        p++;
        *p = (unsigned char)(value >> 16);
        p++;
        *p = (unsigned char)(value >> 8);
        p++;
        *p = (unsigned char)value;
        return 5;
    }
}

static inline long long asn1_decode(const unsigned char* buf, int* len) {
    long long ret = 0;
    int bytes = 0;
    unsigned int tag = 0, lenBytes = 0;
    const unsigned char* p = buf;

    tag = *p;
    p++;
    if (tag < 128) {
        // Single-byte data
        if (len) *len = 1;
        return tag;
    }
    else if (tag == 0x80) {
        // invalid data
        if (len) *len = -1;
        return 0;
    }

    // Multi-byte data
    bytes++;

    lenBytes = tag & 0x7F;

    for (ret = 0; lenBytes > 0; lenBytes--) {
        if (len && *len && bytes == *len) {
            // Not enough length
            *len = 0;
            return 0;
        }
        ret = (ret << 8) | *p;
        p++;
        bytes++;
    }

    if (len) *len = bytes;
    return ret;
}

#endif // HV_MATH_H_
