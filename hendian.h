#ifndef HW_ENDIAN_H_
#define HW_ENDIAN_H_

#include <stdio.h>
#include <string.h>

#include "hdef.h"

#define LITTLE_ENDIAN   0
#define BIG_ENDIAN      1
#define NET_ENDIAN      BIG_ENDIAN

int check_endian() {
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
uint8* serialize(uint8* buf, T value, int host_endian = LITTLE_ENDIAN, int buf_endian = BIG_ENDIAN) {
    size_t size = sizeof(T);
    uint8* pDst = buf;
    uint8* pSrc = (uint8*)&value;

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
uint8* deserialize(uint8* buf, T* value, int host_endian = LITTLE_ENDIAN, int buf_endian = BIG_ENDIAN) {
    size_t size = sizeof(T);
    uint8* pSrc = buf;
    uint8* pDst = (uint8*)value;

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
