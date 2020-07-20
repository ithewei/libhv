#ifndef HV_SYS_INFO_H_
#define HV_SYS_INFO_H_

#include "hplatform.h"

#ifdef OS_LINUX
#include <sys/sysinfo.h>
#endif

#ifdef OS_DARWIN
#include <mach/mach_host.h>
#include <sys/sysctl.h>
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
    unsigned long total;    // KB
    unsigned long free;     // KB
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
#elif defined(OS_DARWIN)
    uint64_t memsize = 0;
    size_t size = sizeof(memsize);
    int which[2] = {CTL_HW, HW_MEMSIZE};
    sysctl(which, 2, &memsize, &size, NULL, 0);
    mem->total = memsize >> 10;

    vm_statistics_data_t info;
    mach_msg_type_number_t count = sizeof(info) / sizeof(integer_t);
    host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&info, &count);
    mem->free = ((uint64_t)info.free_count * sysconf(_SC_PAGESIZE)) >> 10;
    return 0;
#else
    (void)(mem);
    return -10;
#endif
}

#endif // HV_SYS_INFO_H_
