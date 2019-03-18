#ifndef HW_ERR_H_
#define HW_ERR_H_

#include <errno.h>

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
    F(ERR_PARSE,            1023,   "parse failed")     \
    \
    F(ERR_MALLOC,           1030,   "malloc failed")    \
    F(ERR_FREE,             1031,   "free failed")      \
    \
    F(ERR_TASK_TIMEOUT,     1100,   "task timeout")     \
    F(ERR_TASK_DEQUE_FULL,  1101,   "task deque full")  \
    F(ERR_TASK_NOT_CREATE,  1102,   "task not create")  \
    \
    F(ERR_OPEN_FILE,        1200,   "open file failed") \
    F(ERR_SAVE_FILE,        1201,   "save file failed") \
    \
    F(ERR_OUT_OF_RANGE,     1300,   "out of range")     \
    F(ERR_OVER_LIMIT,       1301,   "over limit")

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
    F(ERR_RESOURCE_NOT_FOUND,   3000, "resource not found") \
    F(ERR_GROUP_NOT_FOUND,      3001, "group not found")    \
    F(ERR_PERSON_NOT_FOUND,     3002, "person not found")   \
    F(ERR_FACE_NOT_FOUND,       3003, "face not found")     \
    F(ERR_DEVICE_NOT_FOUND,     3004, "device not found")

#define FOREACH_ERR_GRPC(F)     \
    F(ERR_GRPC_FIRST,           4000, "grpc error") \
    F(ERR_GRPC_STATUS_CANCELLED,4001, "grpc status cancelled")  \
    F(ERR_GRPC_STATUS_UNKNOWN,  4002, "grpc status unknown")    \
    F(ERR_GRPC_STATUS_INVALID_ARGUMENT,  4003, "grpc status invalid argument") \
    F(ERR_GRPC_STATUS_DEADLINE, 4004, "grpc status deadline")   \
    F(ERR_GRPC_STATUS_NOT_FOUND, 4005, "grpc status not found") \
    F(ERR_GRPC_STATUS_ALREADY_EXISTS, 4006, "grpc status already exists")   \
    F(ERR_GRPC_STATUS_PERMISSION_DENIED, 4007, "grpc status permission denied") \
    F(ERR_GRPC_STATUS_RESOURCE_EXHAUSTED, 4008, "grpc status resource exhausted")    \
    F(ERR_GRPC_STATUS_FAILED_PRECONDITION, 4009, "grpc status failed precondition") \
    F(ERR_GRPC_STATUS_ABORTED, 4010, "grpc status aborted") \
    F(ERR_GRPC_STATUS_OUT_OF_RANGE, 4011, "grpc status out of range")   \
    F(ERR_GRPC_STATUS_UNIMPLEMENTED, 4012, "grpc status unimplemented") \
    F(ERR_GRPC_STATUS_INTERNAL, 4013, "grpc status internal") \
    F(ERR_GRPC_STATUS_UNAVAILABLE, 4014, "grpc service unavailable") \
    F(ERR_GRPC_STATUS_DATA_LOSS, 4015, "grpc status data loss")

#define FOREACH_ERR(F) \
    FOREACH_ERR_COMMON(F) \
    FOREACH_ERR_NETWORK(F)  \
    FOREACH_ERR_SERVICE(F)  \
    FOREACH_ERR_GRPC(F)

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

#endif  // HW_ERR_H_
