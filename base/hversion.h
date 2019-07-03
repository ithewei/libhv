#ifndef HW_VERSION_H_
#define HW_VERSION_H_

#include "hdef.h"

#define H_VERSION_MAJOR   1
#define H_VERSION_MINOR   18
#define H_VERSION_MICRO   5
#define H_VERSION_PATCH   2

#define H_VERSION_STRING    STRINGIFY(H_VERSION_MAJOR) "." \
                            STRINGIFY(H_VERSION_MINOR) "." \
                            STRINGIFY(H_VERSION_MICRO) "." \
                            STRINGIFY(H_VERSION_PATCH)

#define H_VERSION_NUMBER    (H_VERSION_MAJOR << 24) | (H_VERSION_MINOR << 16) | (H_VERSION_MICRO << 8) | H_VERSION_PATCH

inline const char* get_static_version() {
    return H_VERSION_STRING;
}

EXTERN_C const char* get_compile_version();

#endif  // HW_VERSION_H_
