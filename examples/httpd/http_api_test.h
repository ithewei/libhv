#ifndef HTTP_API_TEST_H_
#define HTTP_API_TEST_H_

#include "HttpServer.h"

// XXX(path, method, handler)
#define HTTP_API_MAP(XXX) \
    XXX("/hello",   GET,    http_api_hello)     \
    XXX("/query",   GET,    http_api_query)     \
    XXX("/echo",    POST,   http_api_echo)      \
    XXX("/kv",      POST,   http_api_kv)        \
    XXX("/json",    POST,   http_api_json)      \
    XXX("/form",    POST,   http_api_form)      \
    XXX("/upload",  POST,   http_api_upload)    \
    XXX("/grpc",    POST,   http_api_grpc)      \
    \
    XXX("/test",    POST,   http_api_test)      \
    XXX("/group/:group_name/user/:user_id", DELETE, http_api_restful)   \

inline void response_status(HttpResponse* res, int code, const char* message) {
    res->Set("code", code);
    res->Set("message", message);
}

inline int http_api_preprocessor(HttpRequest* req, HttpResponse* res) {
    //printf("%s:%d\n", req->client_addr.ip.c_str(), req->client_addr.port);
    //printf("%s\n", req->Dump(true, true).c_str());
    req->ParseBody();
    res->content_type = APPLICATION_JSON;
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
    // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
    // ?query => HttpRequest::query_params
    for (auto& param : req->query_params) {
        res->Set(param.first.c_str(), param.second);
    }
    response_status(res, 0, "Query completed.");
    return 0;
}

inline int http_api_echo(HttpRequest* req, HttpResponse* res) {
    res->content_type = req->content_type;
    res->body = req->body;
    return 0;
}

inline int http_api_kv(HttpRequest*req, HttpResponse* res) {
    if (req->content_type != APPLICATION_URLENCODED) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    res->content_type = APPLICATION_URLENCODED;
    res->kv = req->kv;
    return 0;
}

inline int http_api_json(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != APPLICATION_JSON) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    res->content_type = APPLICATION_JSON;
    res->json = req->json;
    return 0;
}

inline int http_api_form(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != MULTIPART_FORM_DATA) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    res->content_type = MULTIPART_FORM_DATA;
    res->form = req->form;
    return 0;
}

inline int http_api_upload(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != MULTIPART_FORM_DATA) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    FormData file = req->form["file"];
    string filepath("html/uploads/");
    filepath += file.filename;
    FILE* fp = fopen(filepath.c_str(), "w");
    if (fp) {
        hlogi("Save as %s", filepath.c_str());
        fwrite(file.content.data(), 1, file.content.size(), fp);
        fclose(fp);
    }
    response_status(res, 0, "OK");
    return 0;
}

inline int http_api_grpc(HttpRequest* req, HttpResponse* res) {
    if (req->content_type != APPLICATION_GRPC) {
        res->status_code = HTTP_STATUS_BAD_REQUEST;
        return 0;
    }
    // parse protobuf: ParseFromString
    // req->body;
    // res->content_type = APPLICATION_GRPC;
    // serailize protobuf: SerializeAsString
    // res->body;
    return 0;
}

inline int http_api_test(HttpRequest* req, HttpResponse* res) {
    string str = req->GetString("string");
    //int64_t n = req->Get<int64_t>("int");
    //double f = req->Get<double>("float");
    //bool b = req->Get<bool>("bool");
    int64_t n = req->GetInt("int");
    double f = req->GetFloat("float");
    bool b = req->GetBool("bool");

    res->content_type = req->content_type;
    res->Set("string", str);
    res->Set("int", n);
    res->Set("float", f);
    res->Set("bool", b);
    response_status(res, 0, "OK");
    return 0;
}

inline int http_api_restful(HttpRequest* req, HttpResponse* res) {
    // RESTful /:field/ => HttpRequest::query_params
    // path=/group/:group_name/user/:user_id
    //string group_name = req->GetParam("group_name");
    //string user_id = req->GetParam("user_id");
    for (auto& param : req->query_params) {
        res->Set(param.first.c_str(), param.second);
    }
    response_status(res, 0, "Operation completed.");
    return 0;
}

#endif // HTTP_API_TEST_H_
