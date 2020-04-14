#ifndef HV_SYS_INFO_H_
#define HV_SYS_INFO_H_

#include "hplatform.h"

#ifdef OS_LINUX
#include <sys/sysinfo.h>
#endif

static inline int get_ncpu() {
#ifdef OS_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#else
    //return get_nprocs();
    //return get_nprocs_conf();
    //return sysconf(_SC_NPROCESSORS_ONLN);     // processors available
    return sysconf(_SC_NPROCESSORS_CONF);     // processors configured
#endif
}

typedef struct meminfo_s {
    unsigned long total; // KB
    unsigned long free; // KB
} meminfo_t;

static inline int get_meminfo(meminfo_t* mem) {
#ifdef OS_WIN
    MEMORYSTATUSEX memstat;
    memset(&memstat, 0, sizeof(memstat));
    memstat.dwLength = sizeof(memstat);
    GlobalMemoryStatusEx(&memstat);
    mem->total = (unsigned long)(memstat.ullTotalPhys >> 10);
    mem->free = (unsigned long)(memstat.ullAvailPhys >> 10);
    return 0;
#elif defined(OS_LINUX)
    struct sysinfo info;
    if (sysinfo(&info) < 0) {
        return errno;
    }
    mem->total = info.totalram * info.mem_unit >> 10;
    mem->free = info.freeram * info.mem_unit >> 10;
    return 0;
#else
    return -10;
#endif
}

#endif // HV_SYS_INFO_H_
