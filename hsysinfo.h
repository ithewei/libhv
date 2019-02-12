#ifndef H_SYS_INFO_H_
#define H_SYS_INFO_H_

#include "hplatform.h"

inline int get_ncpu() {
#ifdef __unix__
    return sysconf(_SC_NPROCESSORS_CONF);
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#endif
}

#endif // H_SYS_INFO_H_
