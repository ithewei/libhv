#include <stdio.h>

#include "hv/HttpService.h"

using namespace hv;

static int specific_handler(HttpRequest* req, HttpResponse* resp) {
    (void)req;
    (void)resp;
    return 200;
}

static int fallback_handler(HttpRequest* req, HttpResponse* resp) {
    (void)req;
    (void)resp;
    return 418;
}

int main() {
    HttpService service;
    service.Any("*", fallback_handler);
    service.GET("/ping", specific_handler);

    http_handler* handler = NULL;
    HttpResponse resp;
    HttpRequest req;

    req.path = "/ping";
    req.method = HTTP_GET;
    if (service.GetRoute(&req, &handler) != 0 || handler == NULL || handler->sync_handler(&req, &resp) != 200) {
        fprintf(stderr, "specific route should win over '*' catch-all\n");
        return -1;
    }

    req.path = "/not-found";
    req.method = HTTP_GET;
    if (service.GetRoute(&req, &handler) != 0 || handler == NULL || handler->sync_handler(&req, &resp) != 418) {
        fprintf(stderr, "catch-all should match unknown path\n");
        return -2;
    }

    return 0;
}
