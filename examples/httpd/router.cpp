#include "router.h"

#include "handler.h"
#include "hthread.h"
#include "hasync.h"     // import hv::async
#include "requests.h"   // import requests::async

void Router::Register(hv::HttpService& router) {
    // preprocessor => Handler => postprocessor
    router.preprocessor = Handler::preprocessor;
    router.postprocessor = Handler::postprocessor;
    // router.errorHandler = Handler::errorHandler;
    // router.largeFileHandler = Handler::sendLargeFile;

    // curl -v http://ip:port/ping
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    // curl -v http://ip:port/data
    router.GET("/data", [](HttpRequest* req, HttpResponse* resp) {
        static char data[] = "0123456789";
        return resp->Data(data, 10 /*, false */);
    });

    // curl -v http://ip:port/html/index.html
    router.GET("/html/index.html", [](HttpRequest* req, HttpResponse* resp) {
        return resp->File("html/index.html");
    });

    // curl -v http://ip:port/paths
    router.GET("/paths", [&router](HttpRequest* req, HttpResponse* resp) {
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

    // curl -v http://ip:port/service
    router.GET("/service", [](const HttpContextPtr& ctx) {
        ctx->setContentType("application/json");
        ctx->set("base_url", ctx->service->base_url);
        ctx->set("document_root", ctx->service->document_root);
        ctx->set("home_page", ctx->service->home_page);
        ctx->set("error_page", ctx->service->error_page);
        ctx->set("index_of", ctx->service->index_of);
        return 200;
    });

    // curl -v http://ip:port/echo -d "hello,world!"
    router.POST("/echo", [](const HttpContextPtr& ctx) {
        return ctx->send(ctx->body(), ctx->type());
    });

    // wildcard *
    // curl -v http://ip:port/wildcard/any
    router.GET("/wildcard*", [](HttpRequest* req, HttpResponse* resp) {
        std::string str = req->path + " match /wildcard*";
        return resp->String(str);
    });

    // curl -v http://ip:port/async
    router.GET("/async", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        writer->Begin();
        writer->WriteHeader("X-Response-tid", hv_gettid());
        writer->WriteHeader("Content-Type", "text/plain");
        writer->WriteBody("This is an async response.\n");
        writer->End();
    });

    // curl -v http://ip:port/www.*
    // curl -v http://ip:port/www.example.com
    router.GET("/www.*", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
        HttpRequestPtr req2(new HttpRequest);
        req2->url = req->path.substr(1);
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
    // bin/curl -v http://ip:port/form -F 'user=admin' -F 'pswd=123456'
    router.POST("/form", Handler::form);

    // curl -v http://ip:port/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'
    // curl -v http://ip:port/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'
    // bin/curl -v http://ip:port/test -F 'bool=1' -F 'int=123' -F 'float=3.14' -F 'string=hello'
    router.POST("/test", Handler::test);

    // Content-Type: application/grpc
    // bin/curl -v --http2 http://ip:port/grpc -H "content-type:application/grpc" -d 'protobuf'
    router.POST("/grpc", Handler::grpc);

    // RESTful API: /group/:group_name/user/:user_id
    // curl -v -X DELETE http://ip:port/group/test/user/123
    router.Delete("/group/:group_name/user/:user_id", Handler::restful);
    // router.Delete("/group/{group_name}/user/{user_id}", Handler::restful);

    // curl -v http://ip:port/login -H "Content-Type:application/json" -d '{"username":"admin","password":"123456"}'
    router.POST("/login", Handler::login);

    // curl -v http://ip:port/upload?filename=LICENSE -d '@LICENSE'
    // curl -v http://ip:port/upload -F 'file=@LICENSE'
    router.POST("/upload", Handler::upload);
    // curl -v http://ip:port/upload/README.md -d '@README.md'
    router.POST("/upload/{filename}", Handler::recvLargeFile);

    // SSE: Server Send Events
    // @test html/EventSource.html EventSource.onmessage
    router.GET("/sse", Handler::sse);
}
