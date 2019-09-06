#ifndef HTTP_CLIENT_H_
#define HTTP_CLIENT_H_

#include "HttpRequest.h"

/*
#include <stdio.h>

#include "http_client.h"

int main(int argc, char* argv[]) {
    HttpRequest req;
    req.method = HTTP_GET;
    req.url = "http://ftp.sjtu.edu.cn/sites/ftp.kernel.org/pub/linux/kernel/";
    HttpResponse res;
    int ret = http_client_send(&req, &res);
    printf("%s\n", req.dump(true,true).c_str());
    if (ret != 0) {
        printf("* Failed:%s:%d\n", http_client_strerror(ret), ret);
    }
    else {
        printf("%s\n", res.dump(true,true).c_str());
    }
    return ret;
}
*/

#define DEFAULT_HTTP_TIMEOUT    10 // s
#define DEFAULT_HTTP_PORT       80
int http_client_send(HttpRequest* req, HttpResponse* res, int timeout = DEFAULT_HTTP_TIMEOUT);
const char* http_client_strerror(int errcode);

// http_session: Connection: keep-alive
typedef struct http_session_s http_session_t;
http_session_t* http_session_new(const char* host, int port = DEFAULT_HTTP_PORT);
int http_session_del(http_session_t* hss);

int http_session_set_timeout(http_session_t* hss, int timeout);
int http_session_clear_headers(http_session_t* hss);
int http_session_set_header(http_session_t* hss, const char* key, const char* value);
int http_session_del_header(http_session_t* hss, const char* key);
const char* http_session_get_header(http_session_t* hss, const char* key);

int http_session_send(http_session_t* hss, HttpRequest* req, HttpResponse* res);

#endif  // HTTP_CLIENT_H_
