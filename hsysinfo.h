#ifndef H_SYS_INFO_H_
#define H_SYS_INFO_H_

#include "hplatform.h"
#include "hfile.h"

// logic processors number
inline int get_ncpu() {
#ifdef __unix__
    return sysconf(_SC_NPROCESSORS_CONF);
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#endif
    return 1;
}

typedef struct meminfo_s {
    unsigned int total; // MB
    unsigned int avail; // MB
} meminfo_t;

inline int get_meminfo(meminfo_t* mem) {
#ifdef __linux__
    // cat /proc/meminfo
    // MemTotal:       16432348 kB
    // MemFree:          180704 kB
    // MemAvailable:   10413256 kB
    // Buffers:         1499936 kB
    // Cached:          7066496 kB
    HFile file;
    if (file.open("/proc/meminfo", "r") != 0) {
        return -10;
    }
    string line;
    char name[64];
    unsigned int num;
    char unit[8];
    file.readline(line);
    sscanf(line.c_str(), "%s %u %s", name, &num, unit);
    //printf("%s %u %s\n", name, num, unit);
    mem->total = num >> 10;
    file.readline(line);
    file.readline(line);
    sscanf(line.c_str(), "%s %u %s", name, &num, unit);
    //printf("%s %u %s\n", name, num, unit);
    mem->avail = num >> 10;
    return 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX memstat;
    memset(&memstat, 0, sizeof(memstat));
    memstat.dwLength = sizeof(memstat);
    GlobalMemoryStatusEx(&memstat);
    mem->total = memstat.ullTotalPhys >> 20;
    mem->avail = memstat.ullAvailPhys >> 20;
    return 0;
#endif
    return -1;
}

#endif // H_SYS_INFO_H_
