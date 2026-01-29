#ifndef HV_HTTP_CLIENT_H_
#define HV_HTTP_CLIENT_H_

#include "hexport.h"
#include "hssl.h"
#include "HttpMessage.h"

/*
#include <stdio.h>

#include "HttpClient.h"

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

typedef struct http_client_s http_client_t;

HV_EXPORT http_client_t* http_client_new(const char* host = NULL, int port = DEFAULT_HTTP_PORT, int https = 0);
HV_EXPORT int http_client_del(http_client_t* cli);
HV_EXPORT const char* http_client_strerror(int errcode);

// timeout: s
HV_EXPORT int http_client_set_timeout(http_client_t* cli, int timeout);

// SSL/TLS
HV_EXPORT int http_client_set_ssl_ctx(http_client_t* cli, hssl_ctx_t ssl_ctx);
// hssl_ctx_new(opt) -> http_client_set_ssl_ctx
HV_EXPORT int http_client_new_ssl_ctx(http_client_t* cli, hssl_ctx_opt_t* opt);

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

// low-level api
// @retval >=0 connfd, <0 error
HV_EXPORT int http_client_connect(http_client_t* cli, const char* host, int port, int https, int timeout);
HV_EXPORT int http_client_send_header(http_client_t* cli, HttpRequest* req);
HV_EXPORT int http_client_send_data(http_client_t* cli, const char* data, int size);
HV_EXPORT int http_client_recv_data(http_client_t* cli, char* data, int size);
HV_EXPORT int http_client_recv_response(http_client_t* cli, HttpResponse* resp);
HV_EXPORT int http_client_close(http_client_t* cli);

namespace hv {

class HttpClient {
public:
    HttpClient(const char* host = NULL, int port = DEFAULT_HTTP_PORT, int https = 0)
        : client_(http_client_new(host, port, https))
    {}

    // timeout: s
    int setTimeout(int timeout) {
        return http_client_set_timeout(client_.get(), timeout);
    }

    // SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx) {
        return http_client_set_ssl_ctx(client_.get(), ssl_ctx);
    }
    int newSslCtx(hssl_ctx_opt_t* opt) {
        return http_client_new_ssl_ctx(client_.get(), opt);
    }

    // headers
    int clearHeaders() {
        return http_client_clear_headers(client_.get());
    }
    int setHeader(const char* key, const char* value) {
        return http_client_set_header(client_.get(), key, value);
    }
    int delHeader(const char* key) {
        return http_client_del_header(client_.get(), key);
    }
    const char* getHeader(const char* key) {
        return http_client_get_header(client_.get(), key);
    }

    // http_proxy
    int setHttpProxy(const char* host, int port) {
        return http_client_set_http_proxy(client_.get(), host, port);
    }
    // https_proxy
    int setHttpsProxy(const char* host, int port) {
        return http_client_set_https_proxy(client_.get(), host, port);
    }
    // no_proxy
    int addNoProxy(const char* host) {
        return http_client_add_no_proxy(client_.get(), host);
    }

    // sync
    int send(HttpRequest* req, HttpResponse* resp) {
        return http_client_send(client_.get(), req, resp);
    }

    // async
    int sendAsync(HttpRequestPtr req, HttpResponseCallback resp_cb = NULL) {
        return http_client_send_async(client_.get(), req, std::move(resp_cb));
    }

    // low-level api
    int connect(const char* host, int port = DEFAULT_HTTP_PORT, int https = 0, int timeout = DEFAULT_HTTP_CONNECT_TIMEOUT) {
        return http_client_connect(client_.get(), host, port, https, timeout);
    }
    int sendHeader(HttpRequest* req) {
        return http_client_send_header(client_.get(), req);
    }
    int sendData(const char* data, int size) {
        return http_client_send_data(client_.get(), data, size);
    }
    int recvData(char* data, int size) {
        return http_client_recv_data(client_.get(), data, size);
    }
    int recvResponse(HttpResponse* resp) {
        return http_client_recv_response(client_.get(), resp);
    }
    int close() {
        return http_client_close(client_.get());        
    }

private:
    struct http_client_deleter {
        void operator()(http_client_t* cli) const {
            http_client_del(cli);
        }
    };
    std::unique_ptr<http_client_t, http_client_deleter> client_;
};

}

#endif // HV_HTTP_CLIENT_H_
