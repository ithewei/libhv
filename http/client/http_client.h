#ifndef HTTP_CLIENT_H_
#define HTTP_CLIENT_H_

#include "HttpMessage.h"

/*
#include <stdio.h>

#include "http_client.h"

int main(int argc, char* argv[]) {
    HttpRequest req;
    req.method = HTTP_GET;
    req.url = "http://ftp.sjtu.edu.cn/sites/ftp.kernel.org/pub/linux/kernel/";
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

#define DEFAULT_HTTP_TIMEOUT    10 // s
typedef struct http_client_s http_client_t;

http_client_t* http_client_new(const char* host = NULL, int port = DEFAULT_HTTP_PORT, int tls = 0);
int http_client_del(http_client_t* cli);
const char* http_client_strerror(int errcode);

int http_client_set_timeout(http_client_t* cli, int timeout);

int http_client_clear_headers(http_client_t* cli);
int http_client_set_header(http_client_t* cli, const char* key, const char* value);
int http_client_del_header(http_client_t* cli, const char* key);
const char* http_client_get_header(http_client_t* cli, const char* key);

int http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* res);

// http_client_new -> http_client_send -> http_client_del
int http_client_send(HttpRequest* req, HttpResponse* res, int timeout = DEFAULT_HTTP_TIMEOUT);

#endif  // HTTP_CLIENT_H_
