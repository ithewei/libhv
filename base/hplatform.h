#ifndef HV_PLATFORM_H_
#define HV_PLATFORM_H_

#include "hconfig.h"

// OS
#if defined(WIN64) || defined(_WIN64)
    #define OS_WIN64
    #define OS_WIN32
#elif defined(WIN32)|| defined(_WIN32)
    #define OS_WIN32
#elif defined(ANDROID) || defined(__ANDROID__)
    #define OS_ANDROID
    #define OS_LINUX
#elif defined(linux) || defined(__linux) || defined(__linux__)
    #define OS_LINUX
#elif defined(__APPLE__) && (defined(__GNUC__) || defined(__xlC__) || defined(__xlc__))
    #include <TargetConditionals.h>
    #if defined(TARGET_OS_MAC) && TARGET_OS_MAC
        #define OS_MAC
    #elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        #define OS_IOS
    #endif
    #define OS_DARWIN
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
    #define OS_FREEBSD
    #define OS_BSD
#elif defined(__NetBSD__)
    #define OS_NETBSD
    #define OS_BSD
#elif defined(__OpenBSD__)
    #define OS_OPENBSD
    #define OS_BSD
#elif defined(sun) || defined(__sun) || defined(__sun__)
    #define OS_SOLARIS
#else
    #error "Unsupported operating system platform!"
#endif

#if defined(OS_WIN32) || defined(OS_WIN64)
    #undef  OS_UNIX
    #define OS_WIN
#else
    #define OS_UNIX
#endif

// CC
// _MSC_VER
#ifdef _MSC_VER
#pragma warning (disable: 4100) // unused param
#pragma warning (disable: 4819) // Unicode
#pragma warning (disable: 4996) // _CRT_SECURE_NO_WARNINGS
#endif

// __MINGW32__
// __GNUC__
#ifdef __GNUC__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif
// __clang__

#ifdef HV_STATICLIB
    #define HV_EXPORT
#elif defined(OS_WIN)
    #if defined(HV_EXPORTS) || defined(hv_EXPORTS)
        #define HV_EXPORT  __declspec(dllexport)
    #else
        #define HV_EXPORT  __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
    #define HV_EXPORT  __attribute__((visibility("default")))
#else
    #define HV_EXPORT
#endif

// ARCH
#if defined(__i386) || defined(__i386__) || defined(_M_IX86)
    #define ARCH_X86
    #define ARCH_X86_32
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
    #define ARCH_X64
    #define ARCH_X86_64
#elif defined(__arm__)
    #define ARCH_ARM
#elif defined(__aarch64__) || defined(__ARM64__)
    #define ARCH_ARM64
#endif

// ENDIAN
#ifndef BIG_ENDIAN
    #define BIG_ENDIAN      4321
#endif
#ifndef LITTLE_ENDIAN
    #define LITTLE_ENDIAN   1234
#endif
#define NET_ENDIAN     BIG_ENDIAN

#ifndef BYTE_ORDER
#if defined(ARCH_X86) || defined(ARCH_X86_64) || defined(__ARMEL__)
    #define BYTE_ORDER LITTLE_ENDIAN
#elif defined(__ARMEB__)
    #define BYTE_ORDER BIG_ENDIAN
#endif
#endif

// headers
#ifdef OS_WIN
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #define _CRT_NONSTDC_NO_DEPRECATE
    #define _CRT_SECURE_NO_WARNINGS
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>   // for inet_pton,inet_ntop
    #include <windows.h>
    #include <process.h>    // for getpid,exec
    #include <direct.h>     // for mkdir,rmdir,chdir,getcwd
    #include <io.h>         // for open,close,read,write,lseek,tell

    #define hv_delay(ms)    Sleep(ms)
    #define hv_mkdir(dir)   mkdir(dir)

    #ifndef S_ISREG
    #define S_ISREG(st_mode) (((st_mode) & S_IFMT) == S_IFREG)
    #endif
    #ifndef S_ISDIR
    #define S_ISDIR(st_mode) (((st_mode) & S_IFMT) == S_IFDIR)
    #endif
#else
    #include <unistd.h>
    #include <dirent.h>     // for mkdir,rmdir,chdir,getcwd

    // socket
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <netinet/udp.h>
    #include <netdb.h>  // for gethostbyname

    #define hv_delay(ms)    usleep((ms)*1000)
    #define hv_mkdir(dir)   mkdir(dir, 0777)
#endif

#ifdef _MSC_VER
    typedef int pid_t;
    typedef int gid_t;
    typedef int uid_t;
    #define strcasecmp  stricmp
    #define strncasecmp strnicmp
#else
    typedef int                 BOOL;
    typedef unsigned char       BYTE;
    typedef unsigned short      WORD;
    typedef void*               HANDLE;
    #include <strings.h>
    #define stricmp     strcasecmp
    #define strnicmp    strncasecmp
#endif

// ANSI C
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <signal.h>

#ifndef __cplusplus
#if HAVE_STDBOOL_H
#include <stdbool.h>
#else
    #ifndef bool
    #define bool char
    #endif

    #ifndef true
    #define ture 1
    #endif

    #ifndef false
    #define false 0
    #endif
#endif
#endif

#if HAVE_STDINT_H
#include <stdint.h>
#elif defined(_MSC_VER) && _MSC_VER < 1700
typedef __int8              int8_t;
typedef __int16             int16_t;
typedef __int32             int32_t;
typedef __int64             int64_t;
typedef unsigned __int8     uint8_t;
typedef unsigned __int16    uint16_t;
typedef unsigned __int32    uint32_t;
typedef unsigned __int64    uint64_t;
#endif

typedef float               float32_t;
typedef double              float64_t;

// sizeof(var) = 8
typedef union {
    bool        b;
    char        ch;
    char*       str;
    long long   num;
    float       f;
    double      lf;
    void*       ptr;
} var;

typedef int (*method_t)(void* userdata);
typedef void (*procedure_t)(void* userdata);

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifndef _MSC_VER
#if HAVE_SYS_TIME_H
#include <sys/time.h>   // for gettimeofday
#endif

#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#endif

#endif // HV_PLATFORM_H_
