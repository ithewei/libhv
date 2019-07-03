#ifndef HW_ENDIAN_H_
#define HW_ENDIAN_H_

#include <stdio.h>
#include <string.h>

#include "hplatform.h"
#include "hdef.h"

int detect_endian() {
    union {
        char c;
        short s;
    } u;
    u.s = 0x1122;
    if (u.c == 0x11) {
        return BIG_ENDIAN;
    }
    return LITTLE_ENDIAN;
}

template <typename T>
uint8_t* serialize(uint8_t* buf, T value, int host_endian = LITTLE_ENDIAN, int buf_endian = BIG_ENDIAN) {
    size_t size = sizeof(T);
    uint8_t* pDst = buf;
    uint8_t* pSrc = (uint8_t*)&value;

    if (host_endian == buf_endian) {
        memcpy(pDst, pSrc, size);
    } else {
        for (int i = 0; i < size; ++i) {
            pDst[i] = pSrc[size-i-1];
        }
    }

    return buf+size;
}

template <typename T>
uint8_t* deserialize(uint8_t* buf, T* value, int host_endian = LITTLE_ENDIAN, int buf_endian = BIG_ENDIAN) {
    size_t size = sizeof(T);
    uint8_t* pSrc = buf;
    uint8_t* pDst = (uint8_t*)value;

    if (host_endian == buf_endian) {
        memcpy(pDst, pSrc, size);
    } else {
        for (int i = 0; i < size; ++i) {
            pDst[i] = pSrc[size-i-1];
        }
    }

    return buf+size;
}

#endif  // HW_ENDIAN_H_
