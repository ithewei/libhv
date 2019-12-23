#include "herr.h"

#include <string.h> // for strerror

// errcode => errmsg
const char* hv_strerror(int err) {
    if (err > 0 && err <= SYS_NERR) {
        return strerror(err);
    }

    switch (err) {
#define F(errcode, name, errmsg) \
    case errcode: return errmsg;
    FOREACH_ERR(F)
#undef  F
    default:
        return "Undefined error";
    }
}
