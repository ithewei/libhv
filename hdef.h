#ifndef H_DEF_H
#define H_DEF_H

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

typedef int                 BOOL;

typedef int (*method_t)(void* userdata);
typedef void (*procedure_t)(void* userdata);

#ifndef NULL
#define NULL    0
#endif

#ifndef TRUE
#define TRUE    1L
#endif

#ifndef FALSE
#define FALSE   0L
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

#define SAFE_FREE(p)    do{if (p) {free(p); (p) = NULL;}}while(0)
#define SAFE_DELETE(p)  do{if (p) {delete (p); (p) = NULL;}}while(0)
#define SAFE_DELETE_ARRAY(p) do{if (p) {delete[] (p); (p) = NULL;}}while(0)
#define SAFE_RELEASE(p) do{if (p) {(p)->release(); (p) = NULL;}}while(0)

#ifndef MAKE_FOURCC
#define MAKE_FOURCC(a,b,c,d) \
( ((uint32)d) | ( ((uint32)c) << 8 ) | ( ((uint32)b) << 16 ) | ( ((uint32)a) << 24 ) )
#endif

#define FLOAT_PRECISION 1e-6
#define FLOAT_EQUAL_ZERO(f) (-FLOAT_PRECISION < (f) && (f) < FLOAT_PRECISION)

#define STRINGIFY(x)    STRINGIFY_HELPER(x)
#define STRINGIFY_HELPER(x)    #x

#define STRINGCAT(x,y)  STRINGCAT_HELPER(x,y)
#define STRINGCAT_HELPER(x,y)   x##y

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))

#endif // H_DEF_H