#ifndef HV_REQUESTS_H_
#define HV_REQUESTS_H_

/*
 * Inspired by python requests
 *
 * @code

#include "requests.h"

int main() {
    auto resp = requests::get("http://127.0.0.1:8080/ping");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    resp = requests::post("http://127.0.0.1:8080/echo", "hello,world!");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    return 0;
}

**/

#include <memory>
#include "http_client.h"

namespace requests {

typedef HttpRequestPtr          Request;
typedef HttpResponsePtr         Response;
typedef HttpResponseCallback    ResponseCallback;

HV_INLINE Response request(Request req) {
    Response resp(new HttpResponse);
    int ret = http_client_send(req.get(), resp.get());
    return ret ? NULL : resp;
}

HV_INLINE Response request(http_method method, const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    Request req(new HttpRequest);
    req->method = method;
    req->url = url;
    if (&body != &NoBody) {
        req->body = body;
    }
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }
    return request(req);
}

HV_INLINE Response uploadFile(http_method method, const char* url, const char* filepath, const http_headers& headers = DefaultHeaders) {
    Request req(new HttpRequest);
    req->method = method;
    req->url = url;
    if (req->File(filepath) != 200) return NULL;
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }
    return request(req);
}

#ifndef WITHOUT_HTTP_CONTENT
HV_INLINE Response uploadFormFile(http_method method, const char* url, const char* name, const char* filepath, const http_headers& headers = DefaultHeaders) {
    Request req(new HttpRequest);
    req->method = method;
    req->url = url;
    req->FormFile(name, filepath);
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }
    return request(req);
}
#endif

HV_INLINE Response head(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_HEAD, url, NoBody, headers);
}

HV_INLINE Response get(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_GET, url, NoBody, headers);
}

HV_INLINE Response post(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_POST, url, body, headers);
}

HV_INLINE Response put(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PUT, url, body, headers);
}

HV_INLINE Response patch(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PATCH, url, body, headers);
}

// delete is c++ keyword, we have to replace delete with Delete.
HV_INLINE Response Delete(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_DELETE, url, body, headers);
}

HV_INLINE int async(Request req, ResponseCallback resp_cb) {
    return http_client_send_async(req, resp_cb);
}

}

#endif // HV_REQUESTS_H_
