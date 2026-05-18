#include <assert.h>

#include "hv/HttpService.h"

using namespace hv;

static std::string call_route(HttpService& router, const char* path, http_method method, int* status = NULL) {
    HttpRequest req;
    req.path = path;
    req.method = method;
    http_handler* handler = NULL;
    int ret = router.GetRoute(&req, &handler);
    if (status) *status = ret;
    if (ret != 0 || !handler || !handler->sync_handler) {
        return "";
    }
    HttpResponse resp;
    ret = handler->sync_handler(&req, &resp);
    assert(ret == 200);
    return resp.body;
}

int main(int argc, char** argv) {
    HttpService router;
    router.GET("/status", [](HttpRequest* req, HttpResponse* resp) {
        (void)req;
        resp->body = "EXACT:/status";
        return 200;
    });
    router.GET("/user/:id", [](HttpRequest* req, HttpResponse* resp) {
        resp->body = req->query_params["id"];
        return 200;
    });
    router.Any("*", [](HttpRequest* req, HttpResponse* resp) {
        (void)req;
        resp->body = "FALLBACK";
        return 200;
    });

    int status = 0;
    assert(call_route(router, "/status", HTTP_GET, &status) == "EXACT:/status");
    assert(status == 0);

    assert(call_route(router, "/user/123", HTTP_GET, &status) == "123");
    assert(status == 0);

    assert(call_route(router, "/missing", HTTP_GET, &status) == "FALLBACK");
    assert(status == 0);

    status = 0;
    call_route(router, "/status", HTTP_OPTIONS, &status);
    assert(status == HTTP_STATUS_METHOD_NOT_ALLOWED);

    return 0;
}
