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

Response request(Request req) {
    Response resp = Response(new HttpResponse);
    int ret = http_client_send(req.get(), resp.get());
    return ret ? NULL : resp;
}

Response request(http_method method, const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    Request req = Request(new HttpRequest);
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

Response get(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_GET, url, NoBody, headers);
}

Response options(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_OPTIONS, url, NoBody, headers);
}

Response head(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_HEAD, url, NoBody, headers);
}

Response post(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_POST, url, body, headers);
}

Response put(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PUT, url, body, headers);
}

Response patch(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PATCH, url, body, headers);
}

// delete is c++ keyword, we have to replace delete with Delete.
Response Delete(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_DELETE, url, body, headers);
}

}

#endif // HV_REQUESTS_H_
