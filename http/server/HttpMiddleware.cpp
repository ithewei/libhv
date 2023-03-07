#include "HttpMiddleware.h"
#include "HttpService.h"

BEGIN_NAMESPACE_HV

int HttpMiddleware::CORS(HttpRequest* req, HttpResponse* resp) {
    resp->headers["Access-Control-Allow-Origin"] = req->GetHeader("Origin", "*");
    if (req->method == HTTP_OPTIONS) {
        resp->headers["Access-Control-Allow-Methods"] = req->GetHeader("Access-Control-Request-Method", "OPTIONS, HEAD, GET, POST, PUT, DELETE, PATCH");
        resp->headers["Access-Control-Allow-Headers"] = req->GetHeader("Access-Control-Request-Headers", "Content-Type");
        return HTTP_STATUS_NO_CONTENT;
    }
    return HTTP_STATUS_NEXT;
}

END_NAMESPACE_HV
