#ifndef HW_PLATFORM_H_
#define HW_PLATFORM_H_

#include <sys/types.h>

#ifndef _MSC_VER
#include <sys/time.h>  // for gettimeofday

#include <pthread.h>

#include <strings.h>
#define stricmp     strcasecmp
#define strnicmp    strncasecmp
#endif

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <windows.h>

    #define strcasecmp stricmp
    #define strncasecmp strnicmp
#endif

#ifdef __unix__
    #include <unistd.h>
#endif

#ifdef __GNUC__
    #define GNUC_ALIGN(n)   __attribute__((aligned(n)))
#else
    #define GNUC_ALIGN(n)
#endif

#endif  // HW_PLATFORM_H_
