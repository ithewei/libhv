#ifndef HTTP_API_TEST_H_
#define HTTP_API_TEST_H_

#include "HttpRequest.h"

// XXX(path, method, handler)
#define HTTP_API_MAP(XXX) \
    XXX("/json",    POST,   http_api_json)      \
    XXX("/mp",      POST,   http_api_mp)        \
    XXX("/kv",      POST,   http_api_kv)        \
    XXX("/query",   GET,    http_api_query)     \
    XXX("/echo",    POST,   http_api_echo)      \


inline void http_api_preprocessor(HttpRequest* req, HttpResponse* res) {
    //printf("%s\n", req->dump(true, true).c_str());
    req->parse_url();
    req->parse_body();
}

inline void http_api_postprocessor(HttpRequest* req, HttpResponse* res) {
    res->dump_body();
    //printf("%s\n", res->dump(true, true).c_str());
}

inline void http_api_json(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != APPLICATION_JSON) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return;
    }
    res->json = req->json;
}

inline void http_api_mp(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != MULTIPART_FORM_DATA) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return;
    }
    res->mp = req->mp;
}

inline void http_api_kv(HttpRequest*req, HttpResponse* res) {
    if (req->content_type != X_WWW_FORM_URLENCODED) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return;
    }
    res->kv = req->kv;
}

inline void http_api_query(HttpRequest* req, HttpResponse* res) {
    res->kv = req->query_params;
}

inline void http_api_echo(HttpRequest* req, HttpResponse* res) {
    res->content_type = req->content_type;
    res->body = req->body;
}

#endif // HTTP_API_TEST_H_

