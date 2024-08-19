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
    #warning "Untested operating system platform!"
#endif

#if defined(OS_WIN32) || defined(OS_WIN64)
    #undef  OS_UNIX
    #define OS_WIN
#else
    #undef  OS_WIN
    #define OS_UNIX
#endif

// ARCH
#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
    #define ARCH_X64
    #define ARCH_X86_64
#elif defined(__i386) || defined(__i386__) || defined(_M_IX86)
    #define ARCH_X86
    #define ARCH_X86_32
#elif defined(__aarch64__) || defined(__ARM64__) || defined(_M_ARM64)
    #define ARCH_ARM64
#elif defined(__arm__) || defined(_M_ARM)
    #define ARCH_ARM
#elif defined(__mips64__)
    #define ARCH_MIPS64
#elif defined(__mips__)
    #define ARCH_MIPS
#else
    #warning "Untested hardware architecture!"
#endif

// COMPILER
#if defined (_MSC_VER)
#define COMPILER_MSVC

#if (_MSC_VER < 1200) // Visual C++ 6.0
#define MSVS_VERSION    1998
#define MSVC_VERSION    60
#elif (_MSC_VER >= 1200) && (_MSC_VER < 1300) // Visual Studio 2002, MSVC++ 7.0
#define MSVS_VERSION    2002
#define MSVC_VERSION    70
#elif (_MSC_VER >= 1300) && (_MSC_VER < 1400) // Visual Studio 2003, MSVC++ 7.1
#define MSVS_VERSION    2003
#define MSVC_VERSION    71
#elif (_MSC_VER >= 1400) && (_MSC_VER < 1500) // Visual Studio 2005, MSVC++ 8.0
#define MSVS_VERSION    2005
#define MSVC_VERSION    80
#elif (_MSC_VER >= 1500) && (_MSC_VER < 1600) // Visual Studio 2008, MSVC++ 9.0
#define MSVS_VERSION    2008
#define MSVC_VERSION    90
#elif (_MSC_VER >= 1600) && (_MSC_VER < 1700) // Visual Studio 2010, MSVC++ 10.0
#define MSVS_VERSION    2010
#define MSVC_VERSION    100
#elif (_MSC_VER >= 1700) && (_MSC_VER < 1800) // Visual Studio 2012, MSVC++ 11.0
#define MSVS_VERSION    2012
#define MSVC_VERSION    110
#elif (_MSC_VER >= 1800) && (_MSC_VER < 1900) // Visual Studio 2013, MSVC++ 12.0
#define MSVS_VERSION    2013
#define MSVC_VERSION    120
#elif (_MSC_VER >= 1900) && (_MSC_VER < 1910) // Visual Studio 2015, MSVC++ 14.0
#define MSVS_VERSION    2015
#define MSVC_VERSION    140
#elif (_MSC_VER >= 1910) && (_MSC_VER < 1920) // Visual Studio 2017, MSVC++ 15.0
#define MSVS_VERSION    2017
#define MSVC_VERSION    150
#elif (_MSC_VER >= 1920) && (_MSC_VER < 2000) // Visual Studio 2019, MSVC++ 16.0
#define MSVS_VERSION    2019
#define MSVC_VERSION    160
#endif

#undef  HAVE_STDATOMIC_H
#define HAVE_STDATOMIC_H        0
#undef  HAVE_SYS_TIME_H
#define HAVE_SYS_TIME_H         0
#undef  HAVE_PTHREAD_H
#define HAVE_PTHREAD_H          0

#pragma warning (disable: 4018) // signed/unsigned comparison
#pragma warning (disable: 4100) // unused param
#pragma warning (disable: 4102) // unreferenced label
#pragma warning (disable: 4244) // conversion loss of data
#pragma warning (disable: 4267) // size_t => int
#pragma warning (disable: 4819) // Unicode
#pragma warning (disable: 4996) // _CRT_SECURE_NO_WARNINGS

#elif defined(__GNUC__)
#define COMPILER_GCC

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#elif defined(__clang__)
#define COMPILER_CLANG

#elif defined(__MINGW32__) || defined(__MINGW64__)
#define COMPILER_MINGW

#elif defined(__MSYS__)
#define COMPILER_MSYS

#elif defined(__CYGWIN__)
#define COMPILER_CYGWIN

#else
#warning "Untested compiler!"
#endif

// headers
#ifdef OS_WIN
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
    #elif _WIN32_WINNT < 0x0600
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef _CRT_NONSTDC_NO_DEPRECATE
    #define _CRT_NONSTDC_NO_DEPRECATE
    #endif
    #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
    #endif
    #ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>   // for inet_pton,inet_ntop
    #include <windows.h>
    #include <process.h>    // for getpid,exec
    #include <direct.h>     // for mkdir,rmdir,chdir,getcwd
    #include <io.h>         // for open,close,read,write,lseek,tell

    #define hv_sleep(s)     Sleep((s) * 1000)
    #define hv_msleep(ms)   Sleep(ms)
    #define hv_usleep(us)   Sleep((us) / 1000)
    #define hv_delay(ms)    hv_msleep(ms)
    #define hv_mkdir(dir)   mkdir(dir)

    // access
    #ifndef F_OK
    #define F_OK            0       /* test for existence of file */
    #endif
    #ifndef X_OK
    #define X_OK            (1<<0)  /* test for execute or search permission */
    #endif
    #ifndef W_OK
    #define W_OK            (1<<1)  /* test for write permission */
    #endif
    #ifndef R_OK
    #define R_OK            (1<<2)  /* test for read permission */
    #endif

    // stat
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

    #define hv_sleep(s)     sleep(s)
    #define hv_msleep(ms)   usleep((ms) * 1000)
    #define hv_usleep(us)   usleep(us)
    #define hv_delay(ms)    hv_msleep(ms)
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

// ENDIAN
#ifndef BIG_ENDIAN
#define BIG_ENDIAN      4321
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN   1234
#endif
#ifndef NET_ENDIAN
#define NET_ENDIAN      BIG_ENDIAN
#endif

// BYTE_ORDER
#ifndef BYTE_ORDER
#if defined(__BYTE_ORDER)
    #define BYTE_ORDER  __BYTE_ORDER
#elif defined(__BYTE_ORDER__)
    #define BYTE_ORDER  __BYTE_ORDER__
#elif defined(ARCH_X86)  || defined(ARCH_X86_64)   || \
      defined(__ARMEL__) || defined(__AARCH64EL__) || \
      defined(__MIPSEL)  || defined(__MIPS64EL)
    #define BYTE_ORDER  LITTLE_ENDIAN
#elif defined(__ARMEB__) || defined(__AARCH64EB__) || \
      defined(__MIPSEB)  || defined(__MIPS64EB)
    #define BYTE_ORDER  BIG_ENDIAN
#elif defined(OS_WIN)
    #define BYTE_ORDER  LITTLE_ENDIAN
#else
    #warning "Unknown byte order!"
#endif
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
    #define true 1
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

typedef int (*method_t)(void* userdata);
typedef void (*procedure_t)(void* userdata);

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_SYS_TIME_H
#include <sys/time.h>   // for gettimeofday
#endif

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

#if HAVE_PTHREAD_H
#include <pthread.h>
#endif

#endif // HV_PLATFORM_H_
