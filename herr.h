#ifndef H_ERR_H
#define H_ERR_H

// F(macro, errcode, errmsg)
#define FOREACH_ERR_COMMON(F) \
    F(ERR_OK,               0,      "ok")               \
    F(ERR_UNKNOWN,          1000,   "unknown error")    \
    F(ERR_NULL_PARAM,       1001,   "null param")       \
    F(ERR_NULL_POINTER,     1002,   "null pointer")     \
    F(ERR_NULL_DATA,        1003,   "null data")        \
    \
    F(ERR_INVALID_PARAM,    1010,   "invalid param")    \
    F(ERR_INVALID_HANDLE,   1011,   "invalid handle")   \
    F(ERR_INVALID_JSON,     1012,   "invalid json")     \
    F(ERR_INVALID_XML,      1013,   "invalid xml")      \
    F(ERR_INVALID_FMT,      1014,   "invalid format")   \
    \
    F(ERR_MISMATCH,         1020,   "mismatch")         \
    F(ERR_REQUEST,          1021,   "error request")    \
    F(ERR_RESPONSE,         1022,   "error response")   \
    \
    F(ERR_MALLOC,           1030,   "malloc failed")    \
    F(ERR_FREE,             1031,   "free failed")      \
    \
    F(ERR_TASK_TIMEOUT,     1100,   "task timeout")     \
    F(ERR_TASK_DEQUE_FULL,  1101,   "task deque full")  \
    F(ERR_TASK_NOT_CREATE,  1102,   "task not create")  \
    \
    F(ERR_OPEN_FILE,        1200,   "open file failed") \
    F(ERR_SAVE_FILE,        1201,   "save file failed")

#define FOREACH_ERR_NETWORK(F) \
    F(ERR_ADAPTER_NOT_FOUND,    2001, "adapter not found")  \
    F(ERR_SERVER_NOT_FOUND,     2002, "server not found")   \
    F(ERR_SERVER_UNREACHEABLE,  2003, "server unreacheable")    \
    F(ERR_SERVER_DISCONNECT,    2004, "server disconnect")      \
    F(ERR_CONNECT_TIMEOUT,      2005, "connect timeout")        \
    F(ERR_INVALID_PACKAGE,      2006, "invalid package")        \
    F(ERR_SERVER_NOT_STARTUP,   2007, "server not startup")     \
    F(ERR_CLIENT_DISCONNECT,    2008, "client disconnect")

#define FOREACH_ERR_SERVICE(F)  \
    F(ERR_GROUP_NOT_FOUND,      3000, "group not found")        

#define FOREACH_ERR(F) \
    FOREACH_ERR_COMMON(F) \
    FOREACH_ERR_NETWORK(F)  \
    FOREACH_ERR_SERVICE(F)

#define ENUM_ERR(macro, errcode, _) macro = errcode,
enum E_ERR{
    FOREACH_ERR(ENUM_ERR)
    ERR_LAST
};
#undef ENUM_ERR

// id => errcode
void set_id_errcode(int id, int errcode);
int  get_id_errcode(int id);

// id = gettid()
void set_last_errcode(int errcode);
int  get_last_errcode();

// errcode => errmsg
const char* get_errmsg(int errcode);

#endif // H_ERR_H
