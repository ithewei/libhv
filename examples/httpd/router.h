#ifndef HV_HTTPD_ROUTER_H
#define HV_HTTPD_ROUTER_H

#include "HttpService.h"

#include "handler.h"

class Router {
public:
    static void Register(HttpService& http) {
        // preprocessor => Handler => postprocessor
        http.preprocessor = Handler::preprocessor;
        http.postprocessor = Handler::postprocessor;

        // curl -v http://ip:port/ping
        http.GET("/ping", [](HttpRequest* req, HttpResponse* res) {
            res->body = "PONG";
            return 200;
        });

        // curl -v http://ip:port/echo -d "hello,world!"
        http.POST("/echo", [](HttpRequest* req, HttpResponse* res) {
            res->content_type = req->content_type;
            res->body = req->body;
            return 200;
        });

        // curl -v http://ip:port/sleep?t=3
        http.GET("/sleep", Handler::sleep);

        // curl -v http://ip:port/query?page_no=1\&page_size=10
        http.GET("/query", Handler::query);

        // Content-Type: application/x-www-form-urlencoded
        // curl -v http://ip:port/kv -H "content-type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
        http.POST("/kv", Handler::kv);

        // Content-Type: application/json
        // curl -v http://ip:port/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
        http.POST("/json", Handler::json);

        // Content-Type: multipart/form-data
        // bin/curl -v localhost:8080/form -F "user=admin pswd=123456"
        http.POST("/form", Handler::form);

        // curl -v http://ip:port/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'
        // curl -v http://ip:port/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'
        // bin/curl -v http://ip:port/test -F 'bool=1 int=123 float=3.14 string=hello'
        http.POST("/test", Handler::test);

        // Content-Type: application/grpc
        // bin/curl -v --http2 http://ip:port/grpc -H "content-type:application/grpc" -d 'protobuf'
        http.POST("/grpc", Handler::grpc);

        // RESTful API: /group/:group_name/user/:user_id
        // curl -v -X DELETE http://ip:port/group/test/user/123
        http.Delete("/group/:group_name/user/:user_id", Handler::restful);

        // bin/curl -v localhost:8080/upload -F "file=@LICENSE"
        http.POST("/upload", Handler::upload);

        // curl -v http://ip:port/login -H "Content-Type:application/json" -d '{"username":"admin","password":"123456"}'
        http.POST("/login", Handler::login);
    }
};

#endif // HV_HTTPD_ROUTER_H
