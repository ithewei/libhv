#ifndef HV_HTTPD_HANDLER_H
#define HV_HTTPD_HANDLER_H

#include "HttpService.h"

class Handler {
public:
    // preprocessor => api_handlers => postprocessor
    static int preprocessor(HttpRequest* req, HttpResponse* resp);
    static int postprocessor(HttpRequest* req, HttpResponse* resp);
    static int errorHandler(const HttpContextPtr& ctx);

    static int sleep(const HttpRequestPtr& req, const HttpResponseWriterPtr& writer);
    static int setTimeout(const HttpContextPtr& ctx);
    static int query(const HttpContextPtr& ctx);

    static int kv(HttpRequest* req, HttpResponse* resp);
    static int json(HttpRequest* req, HttpResponse* resp);
    static int form(HttpRequest* req, HttpResponse* resp);
    static int grpc(HttpRequest* req, HttpResponse* resp);

    static int test(const HttpContextPtr& ctx);
    static int restful(const HttpContextPtr& ctx);

    static int login(const HttpContextPtr& ctx);
    static int upload(const HttpContextPtr& ctx);
    // SSE: Server Send Events
    static int sse(const HttpContextPtr& ctx);

    // LargeFile
    static int sendLargeFile(const HttpContextPtr& ctx);
    static int recvLargeFile(const HttpContextPtr& ctx, http_parser_state state, const char* data, size_t size);

private:
    static int response_status(HttpResponse* resp, int code = 200, const char* message = NULL) {
        if (message == NULL) message = http_status_str((enum http_status)code);
        resp->Set("code", code);
        resp->Set("message", message);
        return code;
    }
    static int response_status(const HttpResponseWriterPtr& writer, int code = 200, const char* message = NULL) {
        response_status(writer->response.get(), code, message);
        writer->End();
        return code;
    }
    static int response_status(const HttpContextPtr& ctx, int code = 200, const char* message = NULL) {
        response_status(ctx->response.get(), code, message);
        ctx->send();
        return code;
    }
};

#endif // HV_HTTPD_HANDLER_H
