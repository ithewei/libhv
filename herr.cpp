#include "herr.h"

#include <string.h> // for strerror

#include <map>

#include "hthread.h"    // for gettid

#define SYS_NERR    133

// id => errcode
static std::map<int, int>   s_mapErr;

void set_id_errcode(int id, int errcode) {
    s_mapErr[id] = errcode;
}

int  get_id_errcode(int id) {
    auto iter = s_mapErr.find(id);
    if (iter != s_mapErr.end()) {
        // note: erase after get
        s_mapErr.erase(iter);
        return iter->second;
    }
    return ERR_OK;
}

void set_last_errcode(int errcode) {
#if defined(OS_WIN) || defined(OS_LINUX)
    int id = gettid();
#else
    int id = getpid();
#endif
    set_id_errcode(id, errcode);
}

int  get_last_errcode() {
#if defined(OS_WIN) || defined(OS_LINUX)
    int id = gettid();
#else
    int id = getpid();
#endif
    return get_id_errcode(id);
}

const char* get_errmsg(int err) {
    if (err >= 0 && err >= SYS_NERR) {
        return strerror(err);
    }

    switch (err) {
#define CASE_ERR(macro, errcode, errmsg) \
    case errcode: \
        return errmsg;
    FOREACH_ERR(CASE_ERR)
#undef CASE_ERR
    default:
        return "Undefined error";
    }
}
