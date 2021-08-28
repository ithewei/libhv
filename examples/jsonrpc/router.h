#ifndef HV_JSON_RPC_ROUTER_H_
#define HV_JSON_RPC_ROUTER_H_

#include "cJSON.h"

typedef void (*jsonrpc_handler)(cJSON* jreq, cJSON* jres);

typedef struct {
    const char*     method;
    jsonrpc_handler handler;
} jsonrpc_router;

void error_response(cJSON* jres, int code, const char* message);
void not_found(cJSON* jreq, cJSON* jres);
void bad_request(cJSON* jreq, cJSON* jres);

void calc_add(cJSON* jreq, cJSON* jres);
void calc_sub(cJSON* jreq, cJSON* jres);
void calc_mul(cJSON* jreq, cJSON* jres);
void calc_div(cJSON* jreq, cJSON* jres);

#endif // HV_JSON_RPC_ROUTER_H_
