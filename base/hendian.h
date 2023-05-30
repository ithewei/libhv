#ifndef HV_ENDIAN_H_
#define HV_ENDIAN_H_

#include "hplatform.h"
#if defined(OS_MAC)
#include <libkern/OSByteOrder.h>
#define htobe16(v) OSSwapHostToBigInt16(v)
#define htobe32(v) OSSwapHostToBigInt32(v)
#define htobe64(v) OSSwapHostToBigInt64(v)
#define be16toh(v) OSSwapBigToHostInt16(v)
#define be32toh(v) OSSwapBigToHostInt32(v)
#define be64toh(v) OSSwapBigToHostInt64(v)

#define htole16(v) OSSwapHostToLittleInt16(v)
#define htole32(v) OSSwapHostToLittleInt32(v)
#define htole64(v) OSSwapHostToLittleInt64(v)
#define le16toh(v) OSSwapLittleToHostInt16(v)
#define le32toh(v) OSSwapLittleToHostInt32(v)
#define le64toh(v) OSSwapLittleToHostInt64(v)
#elif defined(OS_WIN)

#if _WIN32_WINNT < _WIN32_WINNT_WIN8
    /*
    * Byte order conversion functions for 64-bit integers and 32 + 64 bit
    * floating-point numbers.  IEEE big-endian format is used for the
    * network floating point format.
    */
    #define _WS2_32_WINSOCK_SWAP_LONG(l)                \
                ( ( ((l) >> 24) & 0x000000FFL ) |       \
                ( ((l) >>  8) & 0x0000FF00L ) |       \
                ( ((l) <<  8) & 0x00FF0000L ) |       \
                ( ((l) << 24) & 0xFF000000L ) )

    #define _WS2_32_WINSOCK_SWAP_LONGLONG(l)            \
                ( ( ((l) >> 56) & 0x00000000000000FFLL ) |       \
                ( ((l) >> 40) & 0x000000000000FF00LL ) |       \
                ( ((l) >> 24) & 0x0000000000FF0000LL ) |       \
                ( ((l) >>  8) & 0x00000000FF000000LL ) |       \
                ( ((l) <<  8) & 0x000000FF00000000LL ) |       \
                ( ((l) << 24) & 0x0000FF0000000000LL ) |       \
                ( ((l) << 40) & 0x00FF000000000000LL ) |       \
                ( ((l) << 56) & 0xFF00000000000000LL ) )


    #ifndef htonll
    __inline unsigned __int64 htonll ( unsigned __int64 Value )
    {
        const unsigned __int64 Retval = _WS2_32_WINSOCK_SWAP_LONGLONG (Value);
        return Retval;
    }
    #endif /* htonll */

    #ifndef ntohll
    __inline unsigned __int64 ntohll ( unsigned __int64 Value )
    {
        const unsigned __int64 Retval = _WS2_32_WINSOCK_SWAP_LONGLONG (Value);
        return Retval;
    }
    #endif /* ntohll */

    #ifndef htonf
    __inline unsigned __int32 htonf ( float Value )
    {
        unsigned __int32 Tempval;
        unsigned __int32 Retval;
        Tempval = *(unsigned __int32*)(&Value);
        Retval = _WS2_32_WINSOCK_SWAP_LONG (Tempval);
        return Retval;
    }
    #endif /* htonf */

    #ifndef ntohf
    __inline float ntohf ( unsigned __int32 Value )
    {
        const unsigned __int32 Tempval = _WS2_32_WINSOCK_SWAP_LONG (Value);
        float Retval;
        *((unsigned __int32*)&Retval) = Tempval;
        return Retval;
    }
    #endif /* ntohf */

    #ifndef htond
    __inline unsigned __int64 htond ( double Value )
    {
        unsigned __int64 Tempval;
        unsigned __int64 Retval;
        Tempval = *(unsigned __int64*)(&Value);
        Retval = _WS2_32_WINSOCK_SWAP_LONGLONG (Tempval);
        return Retval;
    }
    #endif /* htond */

    #ifndef ntohd
    __inline double ntohd ( unsigned __int64 Value )
    {
        const unsigned __int64 Tempval = _WS2_32_WINSOCK_SWAP_LONGLONG (Value);
        double Retval;
        *((unsigned __int64*)&Retval) = Tempval;
        return Retval;
    }
    #endif /* ntohd */
#endif

#define htobe16(v) htons(v)
#define htobe32(v) htonl(v)
#define htobe64(v) htonll(v)
#define be16toh(v) ntohs(v)
#define be32toh(v) ntohl(v)
#define be64toh(v) ntohll(v)

#if (BYTE_ORDER == LITTLE_ENDIAN)
#define htole16(v) (v)
#define htole32(v) (v)
#define htole64(v) (v)
#define le16toh(v) (v)
#define le32toh(v) (v)
#define le64toh(v) (v)
#elif (BYTE_ORDER == BIG_ENDIAN)
#define htole16(v) __builtin_bswap16(v)
#define htole32(v) __builtin_bswap32(v)
#define htole64(v) __builtin_bswap64(v)
#define le16toh(v) __builtin_bswap16(v)
#define le32toh(v) __builtin_bswap32(v)
#define le64toh(v) __builtin_bswap64(v)
#endif
#elif HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#else
#warning "Not found endian.h!"
#endif

#define PI8(p)      *(int8_t*)(p)
#define PI16(p)     *(int16_t*)(p)
#define PI32(p)     *(int32_t*)(p)
#define PI64(p)     *(int64_t*)(p)

#define PU8(p)      *(uint8_t*)(p)
#define PU16(p)     *(uint16_t*)(p)
#define PU32(p)     *(uint32_t*)(p)
#define PU64(p)     *(uint64_t*)(p)

#define PF32(p)     *(float*)(p)
#define PF64(p)     *(double*)(p)

#define GET_BE16(p)     be16toh(PU16(p))
#define GET_BE32(p)     be32toh(PU32(p))
#define GET_BE64(p)     be64toh(PU64(p))

#define GET_LE16(p)     le16toh(PU16(p))
#define GET_LE32(p)     le32toh(PU32(p))
#define GET_LE64(p)     le64toh(PU64(p))

#define PUT_BE16(p, v)  PU16(p) = htobe16(v)
#define PUT_BE32(p, v)  PU32(p) = htobe32(v)
#define PUT_BE64(p, v)  PU64(p) = htobe64(v)

#define PUT_LE16(p, v)  PU16(p) = htole16(v)
#define PUT_LE32(p, v)  PU32(p) = htole32(v)
#define PUT_LE64(p, v)  PU64(p) = htole64(v)

// NOTE: uint8_t* p = (uint8_t*)buf;
#define POP_BE8(p, v)   v = *p; ++p
#define POP_BE16(p, v)  v = be16toh(PU16(p)); p += 2
#define POP_BE32(p, v)  v = be32toh(PU32(p)); p += 4
#define POP_BE64(p, v)  v = be64toh(PU64(p)); p += 8

#define POP_LE8(p, v)   v= *p; ++p
#define POP_LE16(p, v)  v = le16toh(PU16(p)); p += 2
#define POP_LE32(p, v)  v = le32toh(PU32(p)); p += 4
#define POP_LE64(p, v)  v = le64toh(PU64(p)); p += 8

#define PUSH_BE8(p, v)  *p = v; ++p
#define PUSH_BE16(p, v) PU16(p) = htobe16(v); p += 2
#define PUSH_BE32(p, v) PU32(p) = htobe32(v); p += 4
#define PUSH_BE64(p, v) PU64(p) = htobe64(v); p += 8

#define PUSH_LE8(p, v)  *p = v; ++p
#define PUSH_LE16(p, v) PU16(p) = htole16(v); p += 2
#define PUSH_LE32(p, v) PU32(p) = htole32(v); p += 4
#define PUSH_LE64(p, v) PU64(p) = htole64(v); p += 8

// NOTE: NET_ENDIAN = BIG_ENDIAN
#define POP8(p, v)      POP_BE8(p, v)
#define POP16(p, v)     POP_BE16(p, v)
#define POP32(p, v)     POP_BE32(p, v)
#define POP64(p, v)     POP_BE64(p, v)
#define POP_N(p, v, n)  memcpy(v, p, n); p += n

#define PUSH8(p, v)     PUSH_BE8(p, v)
#define PUSH16(p, v)    PUSH_BE16(p, v)
#define PUSH32(p, v)    PUSH_BE32(p, v)
#define PUSH64(p, v)    PUSH_BE64(p, v)
#define PUSH_N(p, v, n) memcpy(p, v, n); p += n

static inline int detect_endian() {
    union {
        char c;
        short s;
    } u;
    u.s = 0x1122;
    return u.c ==0x11 ? BIG_ENDIAN : LITTLE_ENDIAN;
}

#ifdef __cplusplus
template <typename T>
uint8_t* serialize(uint8_t* buf, T value, int host_endian = LITTLE_ENDIAN, int buf_endian = BIG_ENDIAN) {
    size_t size = sizeof(T);
    uint8_t* pDst = buf;
    uint8_t* pSrc = (uint8_t*)&value;

    if (host_endian == buf_endian) {
        memcpy(pDst, pSrc, size);
    }
    else {
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
    }
    else {
        for (int i = 0; i < size; ++i) {
            pDst[i] = pSrc[size-i-1];
        }
    }

    return buf+size;
}
#endif // __cplusplus

#endif // HV_ENDIAN_H_
