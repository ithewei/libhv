#ifndef H_PLATFORM_H
#define H_PLATFORM_H

#ifdef _MSC_VER
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <windows.h>
    #undef  WIN32_LEAN_AND_MEAN
#else    
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/time.h>

    #include <strings.h>
    #define stricmp     strcasecmp
    #define strnicmp    strncasecmp
#endif

#ifdef __GNUC__
    #define GNUC_ALIGN(n)   __attribute__((aligned(n))) 
#else
    #define GNUC_ALIGN(n)
#endif

#endif // H_PLATFORM_H