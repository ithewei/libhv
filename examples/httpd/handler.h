#ifndef HV_HTTPD_HANDLER_H
#define HV_HTTPD_HANDLER_H

#include "HttpMessage.h"

class Handler {
public:
    // preprocessor => handler => postprocessor
    static int preprocessor(HttpRequest* req, HttpResponse* res) {
        // printf("%s:%d\n", req->client_addr.ip.c_str(), req->client_addr.port);
        // printf("%s\n", req->Dump(true, true).c_str());
        // if (req->content_type != APPLICATION_JSON) {
        //     return response_status(res, HTTP_STATUS_BAD_REQUEST);
        // }
        req->ParseBody();
        res->content_type = APPLICATION_JSON;
#if 0
        // authentication sample code
        if (strcmp(req->path.c_str(), "/login") != 0) {
            string token = req->GetHeader("token");
            if (token.empty()) {
                response_status(res, 10011, "Miss token");
                return HTTP_STATUS_UNAUTHORIZED;
            }
            else if (strcmp(token.c_str(), "abcdefg") != 0) {
                response_status(res, 10012, "Token wrong");
                return HTTP_STATUS_UNAUTHORIZED;
            }
            return 0;
        }
#endif
        return 0;
    }

    static int postprocessor(HttpRequest* req, HttpResponse* res) {
        // printf("%s\n", res->Dump(true, true).c_str());
        return 0;
    }

    static int sleep(HttpRequest* req, HttpResponse* res) {
        time_t start_time = time(NULL);
        std::string strTime = req->GetParam("t");
        if (!strTime.empty()) {
            int sec = atoi(strTime.c_str());
            if (sec > 0) {
                hv_delay(sec*1000);
            }
        }
        time_t end_time = time(NULL);
        res->Set("start_time", start_time);
        res->Set("end_time", end_time);
        response_status(res, 0, "OK");
        return 200;
    }

    static int query(HttpRequest* req, HttpResponse* res) {
        // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
        // ?query => HttpRequest::query_params
        for (auto& param : req->query_params) {
            res->Set(param.first.c_str(), param.second);
        }
        response_status(res, 0, "OK");
        return 200;
    }

    static int kv(HttpRequest* req, HttpResponse* res) {
        if (req->content_type != APPLICATION_URLENCODED) {
            return response_status(res, HTTP_STATUS_BAD_REQUEST);
        }
        res->content_type = APPLICATION_URLENCODED;
        res->kv = req->kv;
        return 200;
    }

    static int json(HttpRequest* req, HttpResponse* res) {
        if (req->content_type != APPLICATION_JSON) {
            return response_status(res, HTTP_STATUS_BAD_REQUEST);
        }
        res->content_type = APPLICATION_JSON;
        res->json = req->json;
        return 200;
    }

    static int form(HttpRequest* req, HttpResponse* res) {
        if (req->content_type != MULTIPART_FORM_DATA) {
            return response_status(res, HTTP_STATUS_BAD_REQUEST);
        }
        res->content_type = MULTIPART_FORM_DATA;
        res->form = req->form;
        return 200;
    }

    static int test(HttpRequest* req, HttpResponse* res) {
        // bool b = req->Get<bool>("bool");
        // int64_t n = req->Get<int64_t>("int");
        // double f = req->Get<double>("float");
        bool b = req->GetBool("bool");
        int64_t n = req->GetInt("int");
        double f = req->GetFloat("float");
        string str = req->GetString("string");

        res->content_type = req->content_type;
        res->Set("bool", b);
        res->Set("int", n);
        res->Set("float", f);
        res->Set("string", str);
        response_status(res, 0, "OK");
        return 200;
    }

    static int grpc(HttpRequest* req, HttpResponse* res) {
        if (req->content_type != APPLICATION_GRPC) {
            return response_status(res, HTTP_STATUS_BAD_REQUEST);
        }
        // parse protobuf
        // ParseFromString(req->body);
        // res->content_type = APPLICATION_GRPC;
        // serailize protobuf
        // res->body = SerializeAsString(xxx);
        response_status(res, 0, "OK");
        return 200;
    }

    static int restful(HttpRequest* req, HttpResponse* res) {
        // RESTful /:field/ => HttpRequest::query_params
        // path=/group/:group_name/user/:user_id
        // string group_name = req->GetParam("group_name");
        // string user_id = req->GetParam("user_id");
        for (auto& param : req->query_params) {
            res->Set(param.first.c_str(), param.second);
        }
        response_status(res, 0, "OK");
        return 200;
    }

    static int login(HttpRequest* req, HttpResponse* res) {
        string username = req->GetString("username");
        string password = req->GetString("password");
        if (username.empty() || password.empty()) {
            response_status(res, 10001, "Miss username or password");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else if (strcmp(username.c_str(), "admin") != 0) {
            response_status(res, 10002, "Username not exist");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else if (strcmp(password.c_str(), "123456") != 0) {
            response_status(res, 10003, "Password wrong");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else {
            res->Set("token", "abcdefg");
            response_status(res, 0, "OK");
            return HTTP_STATUS_OK;
        }
    }

    static int upload(HttpRequest* req, HttpResponse* res) {
        if (req->content_type != MULTIPART_FORM_DATA) {
            return response_status(res, HTTP_STATUS_BAD_REQUEST);
        }
        FormData file = req->form["file"];
        string filepath("html/uploads/");
        filepath += file.filename;
        FILE* fp = fopen(filepath.c_str(), "w");
        if (fp) {
            fwrite(file.content.data(), 1, file.content.size(), fp);
            fclose(fp);
        }
        response_status(res, 0, "OK");
        return 200;
    }

private:
    static int response_status(HttpResponse* res, int code = 200, const char* message = NULL) {
        res->Set("code", code);
        if (message == NULL) message = http_status_str((enum http_status)code);
        res->Set("message", message);
        res->DumpBody();
        return code;
    }
};

#endif // HV_HTTPD_HANDLER_H
