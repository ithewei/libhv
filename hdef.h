#ifndef HW_DEF_H_
#define HW_DEF_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <map>

typedef unsigned char       uint8;
typedef unsigned short      uint16;
typedef unsigned int        uint32;
typedef unsigned long long  uint64;

typedef char                int8;
typedef short               int16;
typedef int                 int32;
typedef long long           int64;

typedef float               float32;
typedef double              float64;

typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef int                 BOOL;

typedef void*               handle;

typedef int (*method_t)(void* userdata);
typedef void (*procedure_t)(void* userdata);

typedef std::map<std::string, std::string> keyval_t;

#ifndef MAX_PATH
#define MAX_PATH          260
#endif

#ifndef NULL
#ifdef __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif
#endif

#ifndef FALSE
#define FALSE               0
#endif

#ifndef TRUE
#define TRUE                1
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef OPTIONAL
#define OPTIONAL
#endif

#ifndef REQUIRED
#define REQUIRED
#endif

#ifndef REPEATED
#define REPEATED
#endif

#ifndef ABS
#define ABS(n) ((n) < 0 ? -(n) : (n))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(p)    do {if (p) {free(p); (p) = NULL;}}while(0)
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)  do {if (p) {delete (p); (p) = NULL;}}while(0)
#endif

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) do {if (p) {delete[] (p); (p) = NULL;}}while(0)
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do {if (p) {(p)->release(); (p) = NULL;}}while(0)
#endif

#ifndef MAKE_FOURCC
#define MAKE_FOURCC(a, b, c, d) \
( ((uint32)d) | ( ((uint32)c) << 8 ) | ( ((uint32)b) << 16 ) | ( ((uint32)a) << 24 ) )
#endif

#ifndef MAKE_WORD
#define MAKE_WORD(h, l)  ( (((WORD)h) << 8) | (l & 0xff) )
#endif

#ifndef HIBYTE
#define HIBYTE(w) ( (BYTE)(((WORD)w) >> 8) )
#endif

#ifndef LOBYTE
#define LOBYTE(w) ( (BYTE)(w & 0xff) )
#endif

#define MAKE_INT32(h, l)   ( ((int32)h) << 16 | (l & 0xffff) )
#define HIINT16(n)        ( (int16)(((int32)n) >> 16) )
#define LOINI16(n)        ( (int16)(n & 0xffff) )

#define MAKE_INT64(h, l)   ( ((int64)h) << 32 | (l & 0xffffffff) )
#define HIINT32(n)        ( (int32)(((int64)n) >> 32) )
#define LOINI32(n)        ( (int32)(n & 0xffffffff) )

#define FLOAT_PRECISION 1e-6
#define FLOAT_EQUAL_ZERO(f) (-FLOAT_PRECISION < (f) && (f) < FLOAT_PRECISION)

#define STRINGIFY(x)    STRINGIFY_HELPER(x)
#define STRINGIFY_HELPER(x)    #x

#define STRINGCAT(x, y)  STRINGCAT_HELPER(x, y)
#define STRINGCAT_HELPER(x, y)   x##y

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))

#endif  // HW_DEF_H_
