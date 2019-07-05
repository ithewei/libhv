#ifndef HTTP_CLIENT_H_
#define HTTP_CLIENT_H_

#include "HttpRequest.h"

/*
#include <stdio.h>

#include "http_client.h"

int main(int argc, char* argv[]) {
    HttpRequest req;
    req.method = HTTP_GET;
    req.url = "www.baidu.com";
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
int http_client_send(HttpRequest* req, HttpResponse* res, int timeout = DEFAULT_HTTP_TIMEOUT);
const char* http_client_strerror(int errcode);

#endif  // HTTP_CLIENT_H_
