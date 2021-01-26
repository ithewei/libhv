#ifndef HV_HTTPD_HANDLER_H
#define HV_HTTPD_HANDLER_H

#include "HttpMessage.h"

class Handler {
public:
    // preprocessor => handler => postprocessor
    static int preprocessor(HttpRequest* req, HttpResponse* resp) {
        // printf("%s:%d\n", req->client_addr.ip.c_str(), req->client_addr.port);
        // printf("%s\n", req->Dump(true, true).c_str());
        // if (req->content_type != APPLICATION_JSON) {
        //     return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        // }
        req->ParseBody();
        resp->content_type = APPLICATION_JSON;
#if 0
        // authentication sample code
        if (strcmp(req->path.c_str(), "/login") != 0) {
            string token = req->GetHeader("token");
            if (token.empty()) {
                response_status(resp, 10011, "Miss token");
                return HTTP_STATUS_UNAUTHORIZED;
            }
            else if (strcmp(token.c_str(), "abcdefg") != 0) {
                response_status(resp, 10012, "Token wrong");
                return HTTP_STATUS_UNAUTHORIZED;
            }
            return 0;
        }
#endif
        return 0;
    }

    static int postprocessor(HttpRequest* req, HttpResponse* resp) {
        // printf("%s\n", resp->Dump(true, true).c_str());
        return 0;
    }

    static int sleep(HttpRequest* req, HttpResponse* resp) {
        time_t start_time = time(NULL);
        std::string strTime = req->GetParam("t");
        if (!strTime.empty()) {
            int sec = atoi(strTime.c_str());
            if (sec > 0) {
                hv_delay(sec*1000);
            }
        }
        time_t end_time = time(NULL);
        resp->Set("start_time", start_time);
        resp->Set("end_time", end_time);
        response_status(resp, 0, "OK");
        return 200;
    }

    static int query(HttpRequest* req, HttpResponse* resp) {
        // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
        // ?query => HttpRequest::query_params
        for (auto& param : req->query_params) {
            resp->Set(param.first.c_str(), param.second);
        }
        response_status(resp, 0, "OK");
        return 200;
    }

    static int kv(HttpRequest* req, HttpResponse* resp) {
        if (req->content_type != APPLICATION_URLENCODED) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        resp->content_type = APPLICATION_URLENCODED;
        resp->kv = req->kv;
        resp->kv["int"] = hv::to_string(123);
        resp->kv["float"] = hv::to_string(3.14);
        resp->kv["string"] = "hello";
        return 200;
    }

    static int json(HttpRequest* req, HttpResponse* resp) {
        if (req->content_type != APPLICATION_JSON) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        resp->content_type = APPLICATION_JSON;
        resp->json = req->json;
        resp->json["int"] = 123;
        resp->json["float"] = 3.14;
        resp->json["string"] = "hello";
        return 200;
    }

    static int form(HttpRequest* req, HttpResponse* resp) {
        if (req->content_type != MULTIPART_FORM_DATA) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        resp->content_type = MULTIPART_FORM_DATA;
        resp->form = req->form;
        resp->form["int"] = 123;
        resp->form["float"] = 3.14;
        resp->form["float"] = "hello";
        // resp->form["file"] = FormData(NULL, "test.jpg");
        // resp->UploadFormFile("file", "test.jpg");
        return 200;
    }

    static int test(HttpRequest* req, HttpResponse* resp) {
        // bool b = req->Get<bool>("bool");
        // int64_t n = req->Get<int64_t>("int");
        // double f = req->Get<double>("float");
        bool b = req->GetBool("bool");
        int64_t n = req->GetInt("int");
        double f = req->GetFloat("float");
        string str = req->GetString("string");

        resp->content_type = req->content_type;
        resp->Set("bool", b);
        resp->Set("int", n);
        resp->Set("float", f);
        resp->Set("string", str);
        response_status(resp, 0, "OK");
        return 200;
    }

    static int grpc(HttpRequest* req, HttpResponse* resp) {
        if (req->content_type != APPLICATION_GRPC) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        // parse protobuf
        // ParseFromString(req->body);
        // resp->content_type = APPLICATION_GRPC;
        // serailize protobuf
        // resp->body = SerializeAsString(xxx);
        response_status(resp, 0, "OK");
        return 200;
    }

    static int restful(HttpRequest* req, HttpResponse* resp) {
        // RESTful /:field/ => HttpRequest::query_params
        // path=/group/:group_name/user/:user_id
        // string group_name = req->GetParam("group_name");
        // string user_id = req->GetParam("user_id");
        for (auto& param : req->query_params) {
            resp->Set(param.first.c_str(), param.second);
        }
        response_status(resp, 0, "OK");
        return 200;
    }

    static int login(HttpRequest* req, HttpResponse* resp) {
        string username = req->GetString("username");
        string password = req->GetString("password");
        if (username.empty() || password.empty()) {
            response_status(resp, 10001, "Miss username or password");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else if (strcmp(username.c_str(), "admin") != 0) {
            response_status(resp, 10002, "Username not exist");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else if (strcmp(password.c_str(), "123456") != 0) {
            response_status(resp, 10003, "Password wrong");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else {
            resp->Set("token", "abcdefg");
            response_status(resp, 0, "OK");
            return HTTP_STATUS_OK;
        }
    }

    static int upload(HttpRequest* req, HttpResponse* resp) {
        // return resp->SaveFormFile("file", "html/uploads/test.jpg");
        if (req->content_type != MULTIPART_FORM_DATA) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        FormData file = req->form["file"];
        if (file.content.empty()) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        string filepath("html/uploads/");
        filepath += file.filename;
        FILE* fp = fopen(filepath.c_str(), "wb");
        if (fp) {
            fwrite(file.content.data(), 1, file.content.size(), fp);
            fclose(fp);
        }
        response_status(resp, 0, "OK");
        return 200;
    }

private:
    static int response_status(HttpResponse* resp, int code = 200, const char* message = NULL) {
        resp->Set("code", code);
        if (message == NULL) message = http_status_str((enum http_status)code);
        resp->Set("message", message);
        resp->DumpBody();
        return code;
    }
};

#endif // HV_HTTPD_HANDLER_H
