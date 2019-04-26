#include "herr.h"

#include <string.h> // for strerror

#ifndef SYS_NERR
#define SYS_NERR    133
#endif

// errcode => errmsg
const char* get_errmsg(int err) {
    if (err >= 0 && err <= SYS_NERR) {
        return strerror(err);
    }

    switch (err) {
#define CASE_ERR(macro, errcode, errmsg) \
    case errcode: return errmsg;
    FOREACH_ERR(CASE_ERR)
#undef CASE_ERR
    default:
        return "Undefined error";
    }
}
