#ifndef HTTP_HANDLER_H_
#define HTTP_HANDLER_H_

#include "HttpService.h"
#include "HttpParser.h"
#include "FileCache.h"

class HttpHandler {
public:
    // peeraddr
    char                    ip[64];
    int                     port;
    // for handle_request
    HttpService*            service;
    HttpParser*             parser;
    FileCache*              files;
    HttpRequest             req;
    HttpResponse            res;
    file_cache_t*           fc;

    HttpHandler() {
        service = NULL;
        parser = NULL;
        files = NULL;
        fc = NULL;
    }

    ~HttpHandler() {
    }

    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleRequest();

    void Reset() {
        req.Reset();
        res.Reset();
        fc = NULL;
    }
};

#endif // HTTP_HANDLER_H_
