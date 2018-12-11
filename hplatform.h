#ifndef HW_PLATFORM_H_
#define HW_PLATFORM_H_

#ifdef _MSC_VER
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <windows.h>
    #undef  WIN32_LEAN_AND_MEAN

    #define strcasecmp stricmp
    #define strncasecmp strnicmp
#else
    #include <sys/types.h>
    #include <sys/time.h>  // for gettimeofday
    #include <unistd.h>
    #include <pthread.h>

    #include <strings.h>
    #define stricmp     strcasecmp
    #define strnicmp    strncasecmp
#endif

#ifdef __GNUC__
    #define GNUC_ALIGN(n)   __attribute__((aligned(n)))
#else
    #define GNUC_ALIGN(n)
#endif

#endif  // HW_PLATFORM_H_
