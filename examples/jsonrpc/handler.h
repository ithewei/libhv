#ifndef HV_JSON_RPC_HANDLER_H_
#define HV_JSON_RPC_HANDLER_H_

#include "router.h"

void error_response(cJSON* jres, int code, const char* message) {
    cJSON* jerror = cJSON_CreateObject();
    cJSON_AddItemToObject(jerror, "code", cJSON_CreateNumber(code));
    cJSON_AddItemToObject(jerror, "message", cJSON_CreateString(message));
    cJSON_AddItemToObject(jres, "error", jerror);
}

void not_found(cJSON* jreq, cJSON* jres) {
    error_response(jres, 404, "Not Found");
}

void bad_request(cJSON* jreq, cJSON* jres) {
    error_response(jres, 400, "Bad Request");
}

void calc_add(cJSON* jreq, cJSON* jres) {
    cJSON* jparams = cJSON_GetObjectItem(jreq, "params");
    if (cJSON_GetArraySize(jparams) != 2) {
        bad_request(jreq, jres);
        return;
    }
    cJSON* jnum1 = cJSON_GetArrayItem(jparams, 0);
    int num1 = cJSON_GetNumberValue(jnum1);
    cJSON* jnum2 = cJSON_GetArrayItem(jparams, 1);
    int num2 = cJSON_GetNumberValue(jnum2);
    int result = num1 + num2;
    cJSON_AddItemToObject(jres, "result", cJSON_CreateNumber(result));
}

void calc_sub(cJSON* jreq, cJSON* jres) {
    cJSON* jparams = cJSON_GetObjectItem(jreq, "params");
    if (cJSON_GetArraySize(jparams) != 2) {
        bad_request(jreq, jres);
        return;
    }
    cJSON* jnum1 = cJSON_GetArrayItem(jparams, 0);
    int num1 = cJSON_GetNumberValue(jnum1);
    cJSON* jnum2 = cJSON_GetArrayItem(jparams, 1);
    int num2 = cJSON_GetNumberValue(jnum2);
    int result = num1 - num2;
    cJSON_AddItemToObject(jres, "result", cJSON_CreateNumber(result));
}

void calc_mul(cJSON* jreq, cJSON* jres) {
    cJSON* jparams = cJSON_GetObjectItem(jreq, "params");
    if (cJSON_GetArraySize(jparams) != 2) {
        bad_request(jreq, jres);
        return;
    }
    cJSON* jnum1 = cJSON_GetArrayItem(jparams, 0);
    int num1 = cJSON_GetNumberValue(jnum1);
    cJSON* jnum2 = cJSON_GetArrayItem(jparams, 1);
    int num2 = cJSON_GetNumberValue(jnum2);
    int result = num1 * num2;
    cJSON_AddItemToObject(jres, "result", cJSON_CreateNumber(result));
}

void calc_div(cJSON* jreq, cJSON* jres) {
    cJSON* jparams = cJSON_GetObjectItem(jreq, "params");
    if (cJSON_GetArraySize(jparams) != 2) {
        bad_request(jreq, jres);
        return;
    }
    cJSON* jnum1 = cJSON_GetArrayItem(jparams, 0);
    int num1 = cJSON_GetNumberValue(jnum1);
    cJSON* jnum2 = cJSON_GetArrayItem(jparams, 1);
    int num2 = cJSON_GetNumberValue(jnum2);
    if (num2 == 0) {
        bad_request(jreq, jres);
        return;
    } else {
        int result = num1 / num2;
        cJSON_AddItemToObject(jres, "result", cJSON_CreateNumber(result));
    }
}

#endif // HV_JSON_RPC_ROUTER_H_
