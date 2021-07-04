#ifndef HV_HTTPD_ROUTER_H
#define HV_HTTPD_ROUTER_H

#include <future> // import std::async

#include "HttpService.h"
#include "requests.h"

#include "handler.h"

class Router {
public:
    static void Register(HttpService& router) {
        // 前处理 => 处理 => 后处理
        // preprocessor => Handler => postprocessor
        router.preprocessor = Handler::preprocessor;
        router.postprocessor = Handler::postprocessor;

        // curl -v http://ip:port/ping
        router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
            // 发送字符串
            return resp->String("pong");
        });

        // curl -v http://ip:port/data
        router.GET("/data", [](HttpRequest* req, HttpResponse* resp) {
            static char data[] = "0123456789";
            // 发送二进制数据
            return resp->Data(data, 10);
        });

        // curl -v http://ip:port/html/index.html
        router.GET("/html/index.html", [](HttpRequest* req, HttpResponse* resp) {
            // 发送文件内容
            return resp->File("html/index.html");
        });

        // curl -v http://ip:port/paths
        router.GET("/paths", [&router](HttpRequest* req, HttpResponse* resp) {
            // 发送json
            return resp->Json(router.Paths());
        });

        // curl -v http://ip:port/get?env=1
        router.GET("/get", [](HttpRequest* req, HttpResponse* resp) {
            resp->json["origin"] = req->client_addr.ip;
            resp->json["url"] = req->url;
            resp->json["args"] = req->query_params;
            resp->json["headers"] = req->headers;
            return 200;
        });

        // curl -v http://ip:port/echo -d "hello,world!"
        router.POST("/echo", [](HttpRequest* req, HttpResponse* resp) {
            // 回显请求
            resp->content_type = req->content_type;
            resp->body = req->body;
            return 200;
        });

        // 通配符匹配
        // wildcard *
        // curl -v http://ip:port/wildcard/any
        router.GET("/wildcard*", [](HttpRequest* req, HttpResponse* resp) {
            std::string str = req->path + " match /wildcard*";
            return resp->String(str);
        });

        // 异步响应
        // curl -v http://ip:port/async
        router.GET("/async", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
            writer->response->headers["X-Request-tid"] = hv::to_string(hv_gettid());
            std::async([req, writer](){
                writer->Begin();
                writer->response->headers["X-Response-tid"] = hv::to_string(hv_gettid());
                writer->WriteHeader("Content-Type", "text/plain");
                writer->WriteBody("This is an async response.\n");
                writer->End();
            });
        });

        // curl -v http://ip:port/www.*
        // curl -v http://ip:port/www.example.com
        router.GET("/www.*", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
            HttpRequestPtr req2(new HttpRequest);
            req2->url = req->path.substr(1);
            // 异步HTTP客户端请求 + 异步响应
            requests::async(req2, [writer](const HttpResponsePtr& resp2){
                writer->Begin();
                if (resp2 == NULL) {
                    writer->WriteStatus(HTTP_STATUS_NOT_FOUND);
                    writer->WriteHeader("Content-Type", "text/html");
                    writer->WriteBody("<center><h1>404 Not Found</h1></center>");
                } else {
                    writer->WriteResponse(resp2.get());
                }
                writer->End();
            });
        });

        // curl -v http://ip:port/sleep?t=1000
        router.GET("/sleep", Handler::sleep);

        // curl -v http://ip:port/setTimeout?t=1000
        router.GET("/setTimeout", Handler::setTimeout);

        // curl -v http://ip:port/query?page_no=1\&page_size=10
        router.GET("/query", Handler::query);

        // Content-Type: application/x-www-form-urlencoded
        // curl -v http://ip:port/kv -H "content-type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
        router.POST("/kv", Handler::kv);

        // Content-Type: application/json
        // curl -v http://ip:port/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
        router.POST("/json", Handler::json);

        // Content-Type: multipart/form-data
        // bin/curl -v localhost:8080/form -F "user=admin pswd=123456"
        router.POST("/form", Handler::form);

        // curl -v http://ip:port/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'
        // curl -v http://ip:port/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'
        // bin/curl -v http://ip:port/test -F 'bool=1 int=123 float=3.14 string=hello'
        router.POST("/test", Handler::test);

        // Content-Type: application/grpc
        // bin/curl -v --http2 http://ip:port/grpc -H "content-type:application/grpc" -d 'protobuf'
        router.POST("/grpc", Handler::grpc);

        // RESTful API: /group/:group_name/user/:user_id
        // curl -v -X DELETE http://ip:port/group/test/user/123
        router.Delete("/group/:group_name/user/:user_id", Handler::restful);
        // router.Delete("/group/{group_name}/user/{user_id}", Handler::restful);

        // bin/curl -v localhost:8080/upload -F "file=@LICENSE"
        router.POST("/upload", Handler::upload);

        // curl -v http://ip:port/login -H "Content-Type:application/json" -d '{"username":"admin","password":"123456"}'
        router.POST("/login", Handler::login);
    }
};

#endif // HV_HTTPD_ROUTER_H
