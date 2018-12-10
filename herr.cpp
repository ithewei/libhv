#include "herr.h"

#include <map>

#include "hthread.h"    // for gettid

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
    set_id_errcode(gettid(), errcode);
}

int  get_last_errcode() {
    return get_id_errcode(gettid());
}

const char* get_errmsg(int err) {
    switch (err) {
#define CASE_ERR(macro, errcode, errmsg) \
    case errcode: \
        return errmsg;
    FOREACH_ERR(CASE_ERR)
#undef CASE_ERR
    default:
        return "undefined errcode";
    }
}
