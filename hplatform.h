#ifndef HW_PLATFORM_H_
#define HW_PLATFORM_H_

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define _CRT_SECURE_NO_WARNINGS
    #define _CRT_NONSTDC_NO_DEPRECATE
    #include <winsock2.h>
    #include <windows.h>
    #include <direct.h> // for mkdir,rmdir,chdir,getcwd

    #define strcasecmp stricmp
    #define strncasecmp strnicmp
#else
    #include <unistd.h> // for daemon
    #include <dirent.h> // for mkdir,rmdir,chdir,getcwd
    #include <sys/time.h>  // for gettimeofday

    #include <pthread.h>

    #include <strings.h>

    #define stricmp     strcasecmp
    #define strnicmp    strncasecmp
#endif

#ifdef _MSC_VER
    typedef int pid_t;
    typedef int gid_t;
    typedef int uid_t;
#endif

#ifdef __GNUC__
    #define GNUC_ALIGN(n)   __attribute__((aligned(n)))
#else
    #define GNUC_ALIGN(n)
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#ifdef __unix__
#define MKDIR(dir) mkdir(dir, 0777)
#else
#define MKDIR(dir) mkdir(dir)
#endif

#endif  // HW_PLATFORM_H_
