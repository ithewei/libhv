#ifndef HV_REQUESTS_H_
#define HV_REQUESTS_H_

/*
 * Imitate python requests api
 *
 * @code

#include "requests.h"

int main() {
    auto resp = requests::get("/ping");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    auto resp2 = requests::post("/echo", "hello,world!");
    if (resp2 == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp2->status_code, resp2->status_message());
        printf("%s\n", resp2->body.c_str());
    }

    return 0;
}

**/

#include <memory>
#include "http_client.h"

namespace requests {

typedef std::shared_ptr<HttpRequest>  Request;
typedef std::shared_ptr<HttpResponse> Response;

static http_headers DefaultHeaders;
static http_body    NoBody;

static inline Response request(Request req) {
    Response resp(new HttpResponse);
    int ret = http_client_send(req.get(), resp.get());
    return ret ? NULL : resp;
}

static inline Response request(http_method method, const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
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

static inline Response get(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_GET, url, NoBody, headers);
}

static inline Response options(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_OPTIONS, url, NoBody, headers);
}

static inline Response head(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_HEAD, url, NoBody, headers);
}

static inline Response post(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_POST, url, body, headers);
}

static inline Response put(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PUT, url, body, headers);
}

static inline Response patch(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PATCH, url, body, headers);
}

// delete is c++ keyword, we have to replace delete with Delete.
static inline Response Delete(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_DELETE, url, body, headers);
}

}

#endif // HV_REQUESTS_H_
