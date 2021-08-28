#ifndef HV_PROTO_RPC_HANDLER_H_
#define HV_PROTO_RPC_HANDLER_H_

#include "../router.h"

void error_response(protorpc::Response* res, int code, const std::string& message) {
    res->mutable_error()->set_code(code);
    res->mutable_error()->set_message(message);
}

void not_found(const protorpc::Request& req, protorpc::Response* res) {
    error_response(res, 404, "Not Found");
}

void bad_request(const protorpc::Request& req, protorpc::Response* res) {
    error_response(res, 400, "Bad Request");
}

#endif // HV_PROTO_RPC_HANDLER_H_
