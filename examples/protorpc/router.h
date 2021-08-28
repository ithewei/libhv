#ifndef HV_PROTO_RPC_ROUTER_H_
#define HV_PROTO_RPC_ROUTER_H_

#include "generated/base.pb.h"

typedef void (*protorpc_handler)(const protorpc::Request& req, protorpc::Response* res);

typedef struct {
    const char*      method;
    protorpc_handler handler;
} protorpc_router;

void error_response(protorpc::Response* res, int code, const std::string& message);
void not_found(const protorpc::Request& req, protorpc::Response* res);
void bad_request(const protorpc::Request& req, protorpc::Response* res);

void calc_add(const protorpc::Request& req, protorpc::Response* res);
void calc_sub(const protorpc::Request& req, protorpc::Response* res);
void calc_mul(const protorpc::Request& req, protorpc::Response* res);
void calc_div(const protorpc::Request& req, protorpc::Response* res);

void login(const protorpc::Request& req, protorpc::Response* res);

#endif // HV_PROTO_RPC_ROUTER_H_
