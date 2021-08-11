#ifndef HV_ERR_H_
#define HV_ERR_H_

#include <errno.h>

#include "hexport.h"

#ifndef SYS_NERR
#define SYS_NERR    133
#endif

// F(errcode, name, errmsg)
// [1, 133]
#define FOREACH_ERR_SYS(F)

// [1xx~5xx]
#define FOREACH_ERR_STATUS(F)

// [1xxx]
#define FOREACH_ERR_COMMON(F)   \
    F(0,    OK,             "OK")               \
    F(1000, UNKNOWN,        "Unknown error")    \
    \
    F(1001, NULL_PARAM,     "Null parameter")   \
    F(1002, NULL_POINTER,   "Null pointer")     \
    F(1003, NULL_DATA,      "Null data")        \
    F(1004, NULL_HANDLE,    "Null handle")      \
    \
    F(1011, INVALID_PARAM,      "Invalid parameter")\
    F(1012, INVALID_POINTER,    "Invalid pointer")  \
    F(1013, INVALID_DATA,       "Invalid data")     \
    F(1014, INVALID_HANDLE,     "Invalid handle")   \
    F(1015, INVALID_JSON,       "Invalid json")     \
    F(1016, INVALID_XML,        "Invalid xml")      \
    F(1017, INVALID_FMT,        "Invalid format")   \
    F(1018, INVALID_PROTOCOL,   "Invalid protocol") \
    F(1019, INVALID_PACKAGE,    "Invalid package")  \
    \
    F(1021, OUT_OF_RANGE,   "Out of range")     \
    F(1022, OVER_LIMIT,     "Over the limit")       \
    F(1023, MISMATCH,       "Mismatch")         \
    F(1024, PARSE,          "Parse failed")     \
    \
    F(1030, OPEN_FILE,      "Open file failed") \
    F(1031, SAVE_FILE,      "Save file failed") \
    F(1032, READ_FILE,      "Read file failed") \
    F(1033, WRITE_FILE,     "Write file failed")\
    \
    F(1100, TASK_TIMEOUT,       "Task timeout")     \
    F(1101, TASK_QUEUE_FULL,    "Task queue full")  \
    F(1102, TASK_QUEUE_EMPTY,   "Task queue empty") \
    \
    F(1400, REQUEST,        "Bad request")      \
    F(1401, RESPONSE,       "Bad response")     \

// [-1xxx]
#define FOREACH_ERR_FUNC(F)   \
    F(-1001,    MALLOC,     "malloc() error")   \
    F(-1002,    REALLOC,    "realloc() error")  \
    F(-1003,    CALLOC,     "calloc() error")   \
    F(-1004,    FREE,       "free() error")     \
    \
    F(-1011,    SOCKET,     "socket() error")   \
    F(-1012,    BIND,       "bind() error")     \
    F(-1013,    LISTEN,     "listen() error")   \
    F(-1014,    ACCEPT,     "accept() error")   \
    F(-1015,    CONNECT,    "connect() error")  \
    F(-1016,    RECV,       "recv() error")     \
    F(-1017,    SEND,       "send() error")     \
    F(-1018,    RECVFROM,   "recvfrom() error") \
    F(-1019,    SENDTO,     "sendto() error")   \
    F(-1020,    SETSOCKOPT, "setsockopt() error")   \
    F(-1021,    GETSOCKOPT, "getsockopt() error")   \

// grpc [4xxx]
#define FOREACH_ERR_GRPC(F)     \
    F(4000, GRPC_FIRST,                     "grpc no error")                \
    F(4001, GRPC_STATUS_CANCELLED,          "grpc status: cancelled")       \
    F(4002, GRPC_STATUS_UNKNOWN,            "grpc unknown error")           \
    F(4003, GRPC_STATUS_INVALID_ARGUMENT,   "grpc status: invalid argument")\
    F(4004, GRPC_STATUS_DEADLINE,           "grpc status: deadline")        \
    F(4005, GRPC_STATUS_NOT_FOUND,          "grpc status: not found")       \
    F(4006, GRPC_STATUS_ALREADY_EXISTS,     "grpc status: already exists")  \
    F(4007, GRPC_STATUS_PERMISSION_DENIED,  "grpc status: permission denied")   \
    F(4008, GRPC_STATUS_RESOURCE_EXHAUSTED, "grpc status: resource exhausted")  \
    F(4009, GRPC_STATUS_FAILED_PRECONDITION,"grpc status: failed precondition") \
    F(4010, GRPC_STATUS_ABORTED,            "grpc status: aborted")         \
    F(4011, GRPC_STATUS_OUT_OF_RANGE,       "grpc status: out of range")    \
    F(4012, GRPC_STATUS_UNIMPLEMENTED,      "grpc status: unimplemented")   \
    F(4013, GRPC_STATUS_INTERNAL,           "grpc internal error")          \
    F(4014, GRPC_STATUS_UNAVAILABLE,        "grpc service unavailable")     \
    F(4015, GRPC_STATUS_DATA_LOSS,          "grpc status: data loss")       \

#define FOREACH_ERR(F)      \
    FOREACH_ERR_COMMON(F)   \
    FOREACH_ERR_FUNC(F)     \
    FOREACH_ERR_GRPC(F)     \

#undef ERR_OK // prevent conflict
enum {
#define F(errcode, name, errmsg) ERR_##name = errcode,
    FOREACH_ERR(F)
#undef  F
};

#ifdef __cplusplus
extern "C" {
#endif

// errcode => errmsg
HV_EXPORT const char* hv_strerror(int err);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HV_ERR_H_
