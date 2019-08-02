#include "hversion.h"

#include "htime.h"

const char* get_compile_version() {
    static char s_version[64] = {0};
    datetime_t dt = get_compile_datetime();
    snprintf(s_version, sizeof(s_version), "%d.%d.%d.%d",
        H_VERSION_MAJOR, dt.year%100, dt.month, dt.day);
    return s_version;
}
