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
HV_EXPORT int http_client_close(http_client_t* cli);
HV_EXPORT int http_client_del(http_client_t* cli);
HV_EXPORT const char* http_client_strerror(int errcode);

HV_EXPORT int http_client_set_timeout(http_client_t* cli, int timeout);

// common headers
HV_EXPORT int http_client_clear_headers(http_client_t* cli);
HV_EXPORT int http_client_set_header(http_client_t* cli, const char* key, const char* value);
HV_EXPORT int http_client_del_header(http_client_t* cli, const char* key);
HV_EXPORT const char* http_client_get_header(http_client_t* cli, const char* key);

// http_proxy
HV_EXPORT int http_client_set_http_proxy(http_client_t* cli, const char* host, int port);
// https_proxy
HV_EXPORT int http_client_set_https_proxy(http_client_t* cli, const char* host, int port);
// no_proxy
HV_EXPORT int http_client_add_no_proxy(http_client_t* cli, const char* host);

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

namespace hv {

class HttpClient {
public:
    HttpClient(const char* host = NULL, int port = DEFAULT_HTTP_PORT, int https = 0) {
        _client = http_client_new(host, port, https);
    }

    ~HttpClient() {
        if (_client) {
            http_client_del(_client);
            _client = NULL;
        }
    }

    // timeout: s
    int setTimeout(int timeout) {
        return http_client_set_timeout(_client, timeout);
    }

    // headers
    int clearHeaders() {
        return http_client_clear_headers(_client);
    }
    int setHeader(const char* key, const char* value) {
        return http_client_set_header(_client, key, value);
    }
    int delHeader(const char* key) {
        return http_client_del_header(_client, key);
    }
    const char* getHeader(const char* key) {
        return http_client_get_header(_client, key);
    }

    // http_proxy
    int setHttpProxy(const char* host, int port) {
        return http_client_set_http_proxy(_client, host, port);
    }
    // https_proxy
    int setHttpsProxy(const char* host, int port) {
        return http_client_set_https_proxy(_client, host, port);
    }
    // no_proxy
    int addNoProxy(const char* host) {
        return http_client_add_no_proxy(_client, host);
    }

    // sync
    int send(HttpRequest* req, HttpResponse* resp) {
        return http_client_send(_client, req, resp);
    }

    // async
    int sendAsync(HttpRequestPtr req, HttpResponseCallback resp_cb = NULL) {
        return http_client_send_async(_client, req, resp_cb);
    }

    // close
    int close() {
        return http_client_close(_client);
    }

private:
    http_client_t* _client;
};

}

#endif // HV_HTTP_CLIENT_H_
