#ifndef HV_HTTPD_HANDLER_H
#define HV_HTTPD_HANDLER_H

#include <thread>   // import std::thread
#include <chrono>   // import std::chrono

#include "hbase.h"
#include "htime.h"
#include "hfile.h"
#include "hstring.h"
#include "EventLoop.h" // import setTimeout, setInterval
#include "HttpService.h"

class Handler {
public:
    // preprocessor => api_handlers => postprocessor
    static int preprocessor(HttpRequest* req, HttpResponse* resp) {
        // printf("%s:%d\n", req->client_addr.ip.c_str(), req->client_addr.port);
        // printf("%s\n", req->Dump(true, true).c_str());
        // if (req->content_type != APPLICATION_JSON) {
        //     return response_status(resp, HTTP_STATUS_BAD_REQUEST);
        // }
        req->ParseBody();
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
            return HTTP_STATUS_UNFINISHED;
        }
#endif
        return HTTP_STATUS_UNFINISHED;
    }

    static int postprocessor(HttpRequest* req, HttpResponse* resp) {
        // printf("%s\n", resp->Dump(true, true).c_str());
        return resp->status_code;
    }

    static int errorHandler(const HttpContextPtr& ctx) {
        int error_code = ctx->response->status_code;
        return response_status(ctx, error_code);
    }

    static int largeFileHandler(const HttpContextPtr& ctx) {
        std::thread([ctx](){
            ctx->writer->Begin();
            std::string filepath = ctx->service->document_root + ctx->request->Path();
            HFile file;
            if (file.open(filepath.c_str(), "rb") != 0) {
                ctx->writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
                ctx->writer->WriteHeader("Content-Type", "text/html");
                ctx->writer->WriteBody("<center><h1>404 Not Found</h1></center>");
                ctx->writer->End();
                return;
            }
            http_content_type content_type = CONTENT_TYPE_NONE;
            const char* suffix = hv_suffixname(filepath.c_str());
            if (suffix) {
                content_type = http_content_type_enum_by_suffix(suffix);
            }
            if (content_type == CONTENT_TYPE_NONE || content_type == CONTENT_TYPE_UNDEFINED) {
                content_type = APPLICATION_OCTET_STREAM;
            }
            size_t filesize = file.size();
            ctx->writer->WriteHeader("Content-Type", http_content_type_str(content_type));
            ctx->writer->WriteHeader("Content-Length", filesize);
            // ctx->writer->WriteHeader("Transfer-Encoding", "chunked");
            ctx->writer->EndHeaders();

            char* buf = NULL;
            int len = 4096; // 4K
            SAFE_ALLOC(buf, len);
            size_t total_readbytes = 0;
            int last_progress = 0;
            auto start_time = std::chrono::steady_clock::now();
            auto end_time = start_time;
            while (total_readbytes < filesize) {
                size_t readbytes = file.read(buf, len);
                if (readbytes <= 0) {
                    ctx->writer->close();
                    break;
                }
                if (ctx->writer->WriteBody(buf, readbytes) < 0) {
                    break;
                }
                total_readbytes += readbytes;
                int cur_progress = total_readbytes * 100 / filesize;
                if (cur_progress > last_progress) {
                    // printf("<< %s progress: %ld/%ld = %d%%\n",
                    //     ctx->request->path.c_str(), (long)total_readbytes, (long)filesize, (int)cur_progress);
                    last_progress = cur_progress;
                }
                end_time += std::chrono::milliseconds(len / 1024); // 1KB/ms = 1MB/s = 8Mbps
                std::this_thread::sleep_until(end_time);
            }
            ctx->writer->End();
            SAFE_FREE(buf);
            // auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
            // printf("<< %s taked %ds\n", ctx->request->path.c_str(), (int)elapsed_time.count());
        }).detach();
        return HTTP_STATUS_UNFINISHED;
    }

    static int sleep(const HttpContextPtr& ctx) {
        ctx->set("start_ms", gettimeofday_ms());
        std::string strTime = ctx->param("t", "1000");
        if (!strTime.empty()) {
            int ms = atoi(strTime.c_str());
            if (ms > 0) {
                hv_delay(ms);
            }
        }
        ctx->set("end_ms", gettimeofday_ms());
        response_status(ctx, 0, "OK");
        return 200;
    }

    static int setTimeout(const HttpContextPtr& ctx) {
        ctx->set("start_ms", gettimeofday_ms());
        std::string strTime = ctx->param("t", "1000");
        if (!strTime.empty()) {
            int ms = atoi(strTime.c_str());
            if (ms > 0) {
                hv::setTimeout(ms, [ctx](hv::TimerID timerID){
                    ctx->set("end_ms", gettimeofday_ms());
                    response_status(ctx, 0, "OK");
                    ctx->send();
                });
            }
        }
        return HTTP_STATUS_UNFINISHED;
    }

    static int query(const HttpContextPtr& ctx) {
        // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
        // ?query => HttpRequest::query_params
        for (auto& param : ctx->params()) {
            ctx->set(param.first.c_str(), param.second);
        }
        response_status(ctx, 0, "OK");
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
        resp->form["string"] = "hello";
        // resp->form["file"] = FormData(NULL, "test.jpg");
        // resp->UploadFormFile("file", "test.jpg");
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

    static int test(const HttpContextPtr& ctx) {
        ctx->setContentType(ctx->type());
        ctx->set("bool", ctx->get<bool>("bool"));
        ctx->set("int", ctx->get<int>("int"));
        ctx->set("float", ctx->get<float>("float"));
        ctx->set("string", ctx->get("string"));
        response_status(ctx, 0, "OK");
        return 200;
    }

    static int restful(const HttpContextPtr& ctx) {
        // RESTful /:field/ => HttpRequest::query_params
        // path=/group/:group_name/user/:user_id
        std::string group_name = ctx->param("group_name");
        std::string user_id = ctx->param("user_id");
        ctx->set("group_name", group_name);
        ctx->set("user_id", user_id);
        response_status(ctx, 0, "OK");
        return 200;
    }

    static int login(const HttpContextPtr& ctx) {
        string username = ctx->get("username");
        string password = ctx->get("password");
        if (username.empty() || password.empty()) {
            response_status(ctx, 10001, "Miss username or password");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else if (strcmp(username.c_str(), "admin") != 0) {
            response_status(ctx, 10002, "Username not exist");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else if (strcmp(password.c_str(), "123456") != 0) {
            response_status(ctx, 10003, "Password wrong");
            return HTTP_STATUS_BAD_REQUEST;
        }
        else {
            ctx->set("token", "abcdefg");
            response_status(ctx, 0, "OK");
            return HTTP_STATUS_OK;
        }
    }

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
    static int response_status(HttpResponse* resp, int code = 200, const char* message = NULL) {
        if (message == NULL) message = http_status_str((enum http_status)code);
        resp->Set("code", code);
        resp->Set("message", message);
        return code;
    }
    static int response_status(const HttpContextPtr& ctx, int code = 200, const char* message = NULL) {
        if (message == NULL) message = http_status_str((enum http_status)code);
        ctx->set("code", code);
        ctx->set("message", message);
        return code;
    }
};

#endif // HV_HTTPD_HANDLER_H
