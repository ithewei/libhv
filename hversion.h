#ifndef HW_VERSION_H_
#define HW_VERSION_H_

#include <stdio.h>

#include "hdef.h"
#include "htime.h"

#define VERSION_MAJOR   1
#define VERSION_MINOR   18
#define VERSION_MICRO   5
#define VERSION_PATCH   1

#define VERSION_STRING  STRINGIFY(VERSION_MAJOR) "." \
                        STRINGIFY(VERSION_MINOR) "." \
                        STRINGIFY(VERSION_MICRO) "." \
                        STRINGIFY(VERSION_PATCH)

#define VERSION_HEX     (VERSION_MAJOR << 24) | (VERSION_MINOR << 16) | (VERSION_MICRO << 8) | VERSION_PATCH

inline const char* get_static_version() {
    return VERSION_STRING;
}

inline const char* get_compile_version() {
    static char version[64];
    static datetime_t dt = get_compile_datetime();
    snprintf(version, sizeof(version), "%d.%02d.%02d.%02d",
        VERSION_MAJOR, dt.year%100, dt.month, dt.day);
    return version;
}

#endif  // HW_VERSION_H_
