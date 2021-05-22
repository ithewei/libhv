#ifndef HV_DEF_H_
#define HV_DEF_H_

/*
 * @功能：此头文件定义了一些常用宏
 *
 */

#include "hplatform.h"

// 取绝对值
#ifndef ABS
#define ABS(n)  ((n) > 0 ? (n) : -(n))
#endif

// 取负绝对值
#ifndef NABS
#define NABS(n) ((n) < 0 ? (n) : -(n))
#endif

// 数组大小
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif

// 设置bit位
#ifndef BITSET
#define BITSET(p, n) (*(p) |= (1u << (n)))
#endif

// 清除bit位
#ifndef BITCLR
#define BITCLR(p, n) (*(p) &= ~(1u << (n)))
#endif

// 获取bit位
#ifndef BITGET
#define BITGET(i, n) ((i) & (1u << (n)))
#endif

// 换行符
// 因为历史原因，mac使用\r，linux使用\n，windows使用\r\n
#ifndef CR
#define CR      '\r'
#endif

#ifndef LF
#define LF      '\n'
#endif

#ifndef CRLF
#define CRLF    "\r\n"
#endif

// float == 0 的写法
#define FLOAT_PRECISION     1e-6
#define FLOAT_EQUAL_ZERO(f) (ABS(f) < FLOAT_PRECISION)

// 定义一个表示永远的值
#ifndef INFINITE
#define INFINITE    (uint32_t)-1
#endif

// ASCII码
/*
ASCII:
[0, 0x20)    control-charaters
[0x20, 0x7F) printable-charaters

0x0A => LF
0x0D => CR
0x20 => SPACE
0x7F => DEL

[0x09, 0x0D] => \t\n\v\f\r
[0x30, 0x39] => 0~9
[0x41, 0x5A] => A~Z
[0x61, 0x7A] => a~z
*/

// 判断是否为字母
#ifndef IS_ALPHA
#define IS_ALPHA(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#endif

// 判断是否为数字
#ifndef IS_NUM
#define IS_NUM(c)   ((c) >= '0' && (c) <= '9')
#endif

// 判断是否为字母或数字
#ifndef IS_ALPHANUM
#define IS_ALPHANUM(c) (IS_ALPHA(c) || IS_NUM(c))
#endif

// 判断是否为控制符
#ifndef IS_CNTRL
#define IS_CNTRL(c) ((c) >= 0 && (c) < 0x20)
#endif

// 判断是否为可打印符
#ifndef IS_GRAPH
#define IS_GRAPH(c) ((c) >= 0x20 && (c) < 0x7F)
#endif

// 判断是否为16进制
#ifndef IS_HEX
#define IS_HEX(c) (IS_NUM(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#endif

// 判断是否为小写字母
#ifndef IS_LOWER
#define IS_LOWER(c) (((c) >= 'a' && (c) <= 'z'))
#endif

// 判断是否为大写字母
#ifndef IS_UPPER
#define IS_UPPER(c) (((c) >= 'A' && (c) <= 'Z'))
#endif

// 转小写
#ifndef LOWER
#define LOWER(c)    ((c) | 0x20)
#endif

// 转大写
#ifndef UPPER
#define UPPER(c)    ((c) & ~0x20)
#endif

// 定义一些整型显示转换宏
// LD, LU, LLD, LLU for explicit conversion of integer
#ifndef LD
#define LD(v)   ((long)(v))
#endif

#ifndef LU
#define LU(v)   ((unsigned long)(v))
#endif

#ifndef LLD
#define LLD(v)  ((long long)(v))
#endif

#ifndef LLU
#define LLU(v)  ((unsigned long long)(v))
#endif

#ifndef _WIN32

// MAKEWORD, HIBYTE, LOBYTE
#ifndef MAKEWORD
#define MAKEWORD(h, l)  ( (((WORD)h) << 8) | (l & 0xff) )
#endif

#ifndef HIBYTE
#define HIBYTE(w) ( (BYTE)(((WORD)w) >> 8) )
#endif

#ifndef LOBYTE
#define LOBYTE(w) ( (BYTE)(w & 0xff) )
#endif

// MAKELONG, HIWORD, LOWORD
#ifndef MAKELONG
#define MAKELONG(h, l)   ( ((int32_t)h) << 16 | (l & 0xffff) )
#endif

#ifndef HIWORD
#define HIWORD(n)        ( (WORD)(((int32_t)n) >> 16) )
#endif

#ifndef LOWORD
#define LOWORD(n)        ( (WORD)(n & 0xffff) )
#endif

#endif // _WIN32

// MAKEINT64, HIINT, LOINT
#ifndef MAKEINT64
#define MAKEINT64(h, l)   ( ((int64_t)h) << 32 | (l & 0xffffffff) )
#endif

#ifndef HIINT
#define HIINT(n)        ( (int32_t)(((int64_t)n) >> 32) )
#endif

#ifndef LOINT
#define LOINT(n)        ( (int32_t)(n & 0xffffffff) )
#endif

#ifndef MAKE_FOURCC
#define MAKE_FOURCC(a, b, c, d) \
( ((uint32)d) | ( ((uint32)c) << 8 ) | ( ((uint32)b) << 16 ) | ( ((uint32)a) << 24 ) )
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

#ifndef MAX_PATH
#define MAX_PATH    260
#endif

#ifndef NULL
#ifdef __cplusplus
    #define NULL    0
#else
    #define NULL    ((void*)0)
#endif
#endif

#ifndef TRUE
#define TRUE        1
#endif

#ifndef FALSE
#define FALSE       0
#endif

#ifndef SAFE_ALLOC
#define SAFE_ALLOC(p, size)\
    do {\
        void* ptr = malloc(size);\
        if (!ptr) {\
            fprintf(stderr, "malloc failed!\n");\
            exit(-1);\
        }\
        memset(ptr, 0, size);\
        *(void**)&(p) = ptr;\
    } while(0)
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

/*
 * @NOTE: 宏定义中的三个特殊符号：#，##，#@
 * #define TOSTR(x)     #x      // 给x加上双引号
 * #define TOCHAR(x)    #@x     // 给x加上单引号
 * #define STRCAT(x,y)  x##y    // 连接x和y成一个字符串
 *
 */
#define STRINGIFY(x)    STRINGIFY_HELPER(x)
#define STRINGIFY_HELPER(x)     #x

#define STRINGCAT(x, y)  STRINGCAT_HELPER(x, y)
#define STRINGCAT_HELPER(x, y)  x##y

// 获取结构体成员偏移量
#ifndef offsetof
#define offsetof(type, member) \
((size_t)(&((type*)0)->member))
#endif

// 获取结构体成员尾部的偏移量
#ifndef offsetofend
#define offsetofend(type, member) \
(offsetof(type, member) + sizeof(((type*)0)->member))
#endif

// 根据结构体成员指针获取结构体指针
#ifndef container_of
#define container_of(ptr, type, member) \
((type*)((char*)(ptr) - offsetof(type, member)))
#endif

// DEBUG打印宏
#ifdef PRINT_DEBUG
#define printd(...) printf(__VA_ARGS__)
#else
#define printd(...)
#endif

// ERROR打印宏
#ifdef PRINT_ERROR
#define printe(...) fprintf(stderr, __VA_ARGS__)
#else
#define printe(...)
#endif

#endif // HV_DEF_H_
