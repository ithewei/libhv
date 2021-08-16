#ifndef HV_HTTP_CLIENT_H_
#define HV_HTTP_CLIENT_H_

#include "hexport.h"
#include "HttpMessage.h"

/*
#include <stdio.h>

#include "http_client.h"

int main(int argc, char* argv[]) {
    HttpRequest req;
    req.method = HTTP_GET;
    req.url = "http://www.example.com";
    HttpResponse res;
    int ret = http_client_send(&req, &res);
    printf("%s\n", req.Dump(true,true).c_str());
    if (ret != 0) {
        printf("* Failed:%s:%d\n", http_client_strerror(ret), ret);
    }
    else {
        printf("%s\n", res.Dump(true,true).c_str());
    }
    return ret;
}
*/

#define DEFAULT_HTTP_TIMEOUT    60 // s
typedef struct http_client_s http_client_t;

HV_EXPORT http_client_t* http_client_new(const char* host = NULL, int port = DEFAULT_HTTP_PORT, int https = 0);
HV_EXPORT int http_client_del(http_client_t* cli);
HV_EXPORT const char* http_client_strerror(int errcode);

HV_EXPORT int http_client_set_timeout(http_client_t* cli, int timeout);

// common headers
HV_EXPORT int http_client_clear_headers(http_client_t* cli);
HV_EXPORT int http_client_set_header(http_client_t* cli, const char* key, const char* value);
HV_EXPORT int http_client_del_header(http_client_t* cli, const char* key);
HV_EXPORT const char* http_client_get_header(http_client_t* cli, const char* key);

// sync
HV_EXPORT int http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* resp);

// async
// Intern will start an EventLoopThread when http_client_send_async first called,
// http_client_del will destroy the thread.
HV_EXPORT int http_client_send_async(http_client_t* cli, HttpRequestPtr req, HttpResponseCallback resp_cb = NULL);

// top-level api
// http_client_new -> http_client_send -> http_client_del
HV_EXPORT int http_client_send(HttpRequest* req, HttpResponse* resp);
// http_client_send_async(&default_async_client, ...)
HV_EXPORT int http_client_send_async(HttpRequestPtr req, HttpResponseCallback resp_cb = NULL);

#endif // HV_HTTP_CLIENT_H_
