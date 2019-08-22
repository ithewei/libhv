#ifndef HW_DEF_H_
#define HW_DEF_H_

#include "hplatform.h"
#include "hbase.h"

typedef float               float32;
typedef double              float64;

typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef int                 BOOL;

typedef void*               handle;

typedef union {
    bool        b;
    char        ch;
    char*       str;
    long long   num;
    float       f;
    double      lf;
    void*       ptr;
} var;

#ifdef _MSC_VER
typedef int pid_t;
typedef int gid_t;
typedef int uid_t;
#endif

typedef int (*method_t)(void* userdata);
typedef void (*procedure_t)(void* userdata);

#ifndef MAX_PATH
#define MAX_PATH          260
#endif

#ifndef NULL
#define NULL        0
#endif

#ifndef TRUE
#define TRUE        1
#endif

#ifndef FALSE
#define FALSE       0
#endif

#ifndef INFINITE
#define INFINITE    (uint32_t)-1
#endif

#ifndef CR
#define CR      '\r'
#endif

#ifndef LF
#define LF      '\n'
#endif

#ifndef CRLF
#define CRLF    "\r\n"
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

#ifndef LIMIT
#define LIMIT(lower, v, upper) ((v) < (lower) ? (lower) : (v) > (upper) ? (upper) : (v))
#endif

#ifndef BITSET
#define BITSET(p, n) (*(p) |= (1u << (n)))
#endif

#ifndef BITCLEAR
#define BITCLEAR(p, n) (*(p) &= ~(1u << (n)))
#endif

#ifndef BITGET
#define BITGET(i, n) ((i) & (1u << (n)))
#endif

#ifndef LOWER
#define LOWER(c)    ((c) | 0x20)
#endif

#ifndef UPPER
#define UPPER(c)    ((c) & ~0x20)
#endif

#ifndef IS_NUM
#define IS_NUM(c)   ((c) >= '0' && (c) <= '9')
#endif

#ifndef IS_ALPHA
#define IS_ALPHA(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'F'))
#endif

#ifndef IS_ALPHANUM
#define IS_ALPHANUM(c) (IS_NUM(c) || IS_ALPHA(c))
#endif

#ifndef IS_HEX
#define IS_HEX(c) (IS_NUM(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif

#ifndef SAFE_FREE
#define SAFE_FREE(p)    do {if (p) {free(p); (p) = NULL;}} while(0)
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)  do {if (p) {delete (p); (p) = NULL;}} while(0)
#endif

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) do {if (p) {delete[] (p); (p) = NULL;}} while(0)
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do {if (p) {(p)->release(); (p) = NULL;}} while(0)
#endif

#ifndef MAKE_FOURCC
#define MAKE_FOURCC(a, b, c, d) \
( ((uint32)d) | ( ((uint32)c) << 8 ) | ( ((uint32)b) << 16 ) | ( ((uint32)a) << 24 ) )
#endif

#ifndef OS_WIN
#ifndef MAKEWORD
#define MAKEWORD(h, l)  ( (((WORD)h) << 8) | (l & 0xff) )
#endif

#ifndef HIBYTE
#define HIBYTE(w) ( (BYTE)(((WORD)w) >> 8) )
#endif

#ifndef LOBYTE
#define LOBYTE(w) ( (BYTE)(w & 0xff) )
#endif

#ifndef MAKELONG
#define MAKELONG(h, l)   ( ((int32_t)h) << 16 | (l & 0xffff) )
#endif

#ifndef HIWORD
#define HIWORD(n)        ( (WORD)(((int32_t)n) >> 16) )
#endif

#ifndef LOWORD
#define LOWORD(n)        ( (WORD)(n & 0xffff) )
#endif
#endif

#ifndef MAKEINT64
#define MAKEINT64(h, l)   ( ((int64_t)h) << 32 | (l & 0xffffffff) )
#endif

#ifndef HIINT
#define HIINT(n)        ( (int32_t)(((int64_t)n) >> 32) )
#endif

#ifndef LOINT
#define LOINT(n)        ( (int32_t)(n & 0xffffffff) )
#endif

#define FLOAT_PRECISION 1e-6
#define FLOAT_EQUAL_ZERO(f) (ABS(f) < FLOAT_PRECISION)

#define STRINGIFY(x)    STRINGIFY_HELPER(x)
#define STRINGIFY_HELPER(x)    #x

#define STRINGCAT(x, y)  STRINGCAT_HELPER(x, y)
#define STRINGCAT_HELPER(x, y)   x##y

#ifndef offsetof
#define offsetof(type, mmeber) \
((size_t)(&((type*)0)->member))
#endif

#ifndef offsetofend
#define offsetofend(type, member) \
(offsetof(type, member) + sizeof(((type*)0)->member))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
((type*)((char*)(ptr) - offsetof(type, member)))
#endif

#ifndef prefetch
#ifdef __GNUC__
#define prefetch(x) __builtin_prefetch(x)
#else
#define prefetch(x) (void)0
#endif
#endif

// __cplusplus
#ifdef __cplusplus

#include <string>
#include <map>
typedef std::map<std::string, std::string> keyval_t;

#ifndef BEGIN_NAMESPACE
#define BEGIN_NAMESPACE(ns) namespace ns {
#endif

#ifndef END_NAMESPACE
#define END_NAMESPACE(ns)   } // ns
#endif

#ifndef EXTERN_C
#define EXTERN_C            extern "C"
#endif

#ifndef BEGIN_EXTERN_C
#define BEGIN_EXTERN_C      extern "C" {
#endif

#ifndef END_EXTERN_C
#define END_EXTERN_C        } // extern "C"
#endif

#ifndef ENUM
#define ENUM(e)     enum e
#endif

#ifndef STRUCT
#define STRUCT(s)   struct s
#endif

#ifndef DEFAULT
#define DEFAULT(x)  = x
#endif

#else

#define BEGIN_NAMESPACE(ns)
#define END_NAMESPACE(ns)

#define EXTERN_C    extern
#define BEGIN_EXTERN_C
#define END_EXTERN_C

#ifndef ENUM
#define ENUM(e)\
typedef enum e e;\
enum e
#endif

#ifndef STRUCT
#define STRUCT(s)\
typedef struct s s;\
struct s
#endif

#ifndef DEFAULT
#define DEFAULT(x)
#endif

#endif // __cplusplus

#endif  // HW_DEF_H_
