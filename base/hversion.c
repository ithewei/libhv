#include "hversion.h"

#include "htime.h"

const char* hv_compile_version() {
    static char s_version[16] = {0};
    datetime_t dt = hv_compile_datetime();
    snprintf(s_version, sizeof(s_version), "%d.%d.%d.%d",
        HV_VERSION_MAJOR, dt.year%100, dt.month, dt.day);
    return s_version;
}

int version_atoi(const char* str) {
    int hex = 0;

    // trim v1.2.3.4
    const char* pv = strchr(str, 'v');
    const char* pdot = pv ? pv+1 : str;

    while (1) {
        hex = (hex << 8) | atoi(pdot);
        pdot = strchr(pdot, '.');
        if (pdot == NULL)   break;
        ++pdot;
    }

    return hex;
}

void version_itoa(int num, char* str) {
    char* ch = (char*)&num;
    sprintf(str, "%d.%d.%d.%d", ch[3], ch[2], ch[1], ch[0]);

    // trim 0.1.2.3
    const char* p = str;
    while (1) {
        if (p[0] == '0' && p[1] == '.') {
            p += 2;
        }
        else {
            break;
        }
    }

    if (p != str) {
        strcpy(str, p);
    }
}
