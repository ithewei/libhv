#ifndef HV_HTTPD_HANDLER_H
#define HV_HTTPD_HANDLER_H

#include "HttpMessage.h"
#include "HttpResponseWriter.h"
#include "htime.h"
#include "EventLoop.h" // import setTimeout, setInterval

class Handler {
public:
    // preprocessor => api_handlers => postprocessor
    static int preprocessor(HttpRequest* req, HttpResponse* resp) {
        // printf("%s:%d\n", req->client_addr.ip.c_str(), req->client_addr.port);
        // printf("%s\n", req->Dump(true, true).c_str());
        // if (req->content_type != APPLICATION_JSON) {
        //     return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        // }

        // 解析body字符串到对应结构体(json、form、kv)
        req->ParseBody();
        // 响应格式默认为application/json
        resp->content_type = APPLICATION_JSON;
        // cors
        resp->headers["Access-Control-Allow-Origin"] = "*";
        if (req->method == HTTP_OPTIONS) {
            resp->headers["Access-Control-Allow-Origin"] = req->GetHeader("Origin", "*");
            resp->headers["Access-Control-Allow-Methods"] = req->GetHeader("Access-Control-Request-Method", "OPTIONS, HEAD, GET, POST, PUT, DELETE, PATCH");
            resp->headers["Access-Control-Allow-Headers"] = req->GetHeader("Access-Control-Request-Headers", "Content-Type");
            return HTTP_STATUS_NO_CONTENT;
        }
#if 0
        // 前处理中我们可以做一些公共逻辑，如请求统计、请求拦截、API鉴权等，
        // 下面是一段简单的Token头校验代码
        // authentication sample code
        if (strcmp(req->path.c_str(), "/login") != 0) {
            string token = req->GetHeader("token");
            if (token.empty()) {
                response_status(resp, 10011, "Miss token");
                // 返回错误码将直接发送响应，不再走后续的处理
                return HTTP_STATUS_UNAUTHORIZED;
            }
            else if (strcmp(token.c_str(), "abcdefg") != 0) {
                response_status(resp, 10012, "Token wrong");
                return HTTP_STATUS_UNAUTHORIZED;
            }
            return 0;
        }
#endif
        // 返回0表示继续走后面的处理流程
        return 0;
    }

    static int postprocessor(HttpRequest* req, HttpResponse* resp) {
        // 后处理中我们可以做一些后处理公共逻辑，如响应状态码统计、响应数据加密等
        // printf("%s\n", resp->Dump(true, true).c_str());
        return 0;
    }

    static int sleep(HttpRequest* req, HttpResponse* resp) {
        resp->Set("start_ms", gettimeofday_ms());
        std::string strTime = req->GetParam("t");
        if (!strTime.empty()) {
            int ms = atoi(strTime.c_str());
            if (ms > 0) {
                // 该操作会阻塞当前IO线程，仅用做测试接口，实际应用中请勿在回调中做耗时操作
                hv_delay(ms);
            }
        }
        resp->Set("end_ms", gettimeofday_ms());
        response_status(resp, 0, "OK");
        return 200;
    }

    static void setTimeout(const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        writer->response->Set("start_ms", gettimeofday_ms());
        std::string strTime = req->GetParam("t");
        if (!strTime.empty()) {
            int ms = atoi(strTime.c_str());
            if (ms > 0) {
                // 在回调线程中可直接使用setTimeout/setInterval定时器接口
                hv::setTimeout(ms, [writer](hv::TimerID timerID){
                    writer->Begin();
                    HttpResponse* resp = writer->response.get();
                    resp->Set("end_ms", gettimeofday_ms());
                    response_status(resp, 0, "OK");
                    writer->End();
                });
            }
        }
    }

    static int query(HttpRequest* req, HttpResponse* resp) {
        // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
        // ?query => HttpRequest::query_params
        // URL里携带的请求参数已被解析到query_params数据结构里，可通过GetParam("key")获取
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
        // 设置响应格式为application/x-www-form-urlencoded
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
        // 设置响应格式为application/json
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
        // 设置响应格式为multipart/form-data
        resp->content_type = MULTIPART_FORM_DATA;
        resp->form = req->form;
        resp->form["int"] = 123;
        resp->form["float"] = 3.14;
        resp->form["string"] = "hello";
        // 使用formdata格式传输文件
        // resp->form["file"] = FormData(NULL, "test.jpg");
        // resp->UploadFormFile("file", "test.jpg");
        return 200;
    }

    // 通过 Get/Set 接口可以 获取/设置 key:value 到对应数据结构体 json/form/kv
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
        // 需引入protobuf库序列化/反序列化body
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
        // restful风格URL里的参数已被解析到query_params数据结构里，可通过GetParam("key")获取
        std::string group_name = req->GetParam("group_name");
        std::string user_id = req->GetParam("user_id");
        resp->Set("group_name", group_name);
        resp->Set("user_id", user_id);
        response_status(resp, 0, "OK");
        return 200;
    }

    // 登录示例：校验用户名，密码，返回一个token
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

    // 上传文件示例
    static int upload(HttpRequest* req, HttpResponse* resp) {
        // return resp->SaveFormFile("file", "html/uploads/test.jpg");
        if (req->content_type != MULTIPART_FORM_DATA) {
            return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        }
        const FormData& file = req->form["file"];
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
    // 统一的响应格式
    static int response_status(HttpResponse* resp, int code = 200, const char* message = NULL) {
        resp->Set("code", code);
        if (message == NULL) message = http_status_str((enum http_status)code);
        resp->Set("message", message);
        resp->DumpBody();
        return code;
    }
};

#endif // HV_HTTPD_HANDLER_H
