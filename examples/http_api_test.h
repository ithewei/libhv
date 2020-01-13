#ifndef HTTP_API_TEST_H_
#define HTTP_API_TEST_H_

#include "HttpServer.h"

// XXX(path, method, handler)
#define HTTP_API_MAP(XXX) \
    XXX("/hello",   GET,    http_api_hello)     \
    XXX("/query",   GET,    http_api_query)     \
    XXX("/echo",    POST,   http_api_echo)      \
    XXX("/json",    POST,   http_api_json)      \
    XXX("/mp",      POST,   http_api_mp)        \
    XXX("/kv",      POST,   http_api_kv)        \
    XXX("/grpc",    POST,   http_api_grpc)      \
    XXX("/group/:group_name/user/:user_id", DELETE, http_api_restful)   \


inline int http_api_preprocessor(HttpRequest* req, HttpResponse* res) {
    //printf("%s\n", req->Dump(true, true).c_str());
    req->ParseBody();
    return 0;
}

inline int http_api_postprocessor(HttpRequest* req, HttpResponse* res) {
    res->DumpBody();
    //printf("%s\n", res->Dump(true, true).c_str());
    return 0;
}

inline int http_api_hello(HttpRequest* req, HttpResponse* res) {
    res->body = "hello";
    return 0;
}

inline int http_api_query(HttpRequest* req, HttpResponse* res) {
    res->kv = req->query_params;
    return 0;
}

inline int http_api_echo(HttpRequest* req, HttpResponse* res) {
    res->content_type = req->content_type;
    res->body = req->body;
    return 0;
}

inline int http_api_json(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != APPLICATION_JSON) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    res->json = req->json;
    return 0;
}

inline int http_api_mp(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != MULTIPART_FORM_DATA) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    res->mp = req->mp;
    return 0;
}

inline int http_api_kv(HttpRequest*req, HttpResponse* res) {
    if (req->content_type != APPLICATION_URLENCODED) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    res->kv = req->kv;
    return 0;
}

inline int http_api_grpc(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != APPLICATION_GRPC) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    // parse protobuf: ParseFromString
    // req->body;
    // serailize protobuf: SerializeAsString
    // res->body;
    return 0;
}

inline int http_api_restful(HttpRequest*req, HttpResponse* res) {
    // RESTful /:field/ => req->query_params
    res->kv = req->query_params;
    return 0;
}

#endif // HTTP_API_TEST_H_
