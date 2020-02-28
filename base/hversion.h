#ifndef HV_VERSION_H_
#define HV_VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hdef.h"

#define HV_VERSION_MAJOR    1
#define HV_VERSION_MINOR    20
#define HV_VERSION_PATCH    3

#define HV_VERSION_STRING   STRINGIFY(HV_VERSION_MAJOR) "." \
                            STRINGIFY(HV_VERSION_MINOR) "." \
                            STRINGIFY(HV_VERSION_PATCH)

#define HV_VERSION_NUMBER   ((HV_VERSION_MAJOR << 16) | (HV_VERSION_MINOR << 8) | HV_VERSION_PATCH)


static inline const char* hv_version() {
    return HV_VERSION_STRING;
}

const char* hv_compile_version();

// 1.2.3.4 => 0x01020304
int version_atoi(const char* str);

// 0x01020304 => 1.2.3.4
void version_itoa(int hex, char* str);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HV_VERSION_H_
