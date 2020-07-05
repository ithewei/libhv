#ifndef HTTP_HANDLER_H_
#define HTTP_HANDLER_H_

#include "HttpParser.h"
#include "HttpService.h"
#include "FileCache.h"
#include "hloop.h"

#define HTTP_KEEPALIVE_TIMEOUT  75 // s

static inline void on_keepalive_timeout(htimer_t* timer) {
    hio_t* io = (hio_t*)hevent_userdata(timer);
    hio_close(io);
}

class HttpHandler {
public:
    // peeraddr
    char                    ip[64];
    int                     port;
    // for handle_request
    HttpService*            service;
    FileCache*              files;
    HttpParser*             parser;
    HttpRequest             req;
    HttpResponse            res;
    file_cache_t*           fc;
    // for keepalive
    hio_t*                  io;
    htimer_t*               keepalive_timer;

    HttpHandler() {
        service = NULL;
        files = NULL;
        parser = NULL;
        fc = NULL;
        io = NULL;
        keepalive_timer = NULL;
    }

    ~HttpHandler() {
        if (keepalive_timer) {
            htimer_del(keepalive_timer);
            keepalive_timer = NULL;
        }
    }

    // @workflow: preprocessor -> api -> web -> postprocessor
    // @result: HttpRequest -> HttpResponse/file_cache_t
    int HandleRequest();

    void Reset() {
        fc = NULL;
        req.Reset();
        res.Reset();
    }

    void KeepAlive() {
        if (keepalive_timer == NULL) {
            keepalive_timer = htimer_add(hevent_loop(io), on_keepalive_timeout, HTTP_KEEPALIVE_TIMEOUT*1000, 1);
            hevent_set_userdata(keepalive_timer, io);
        }
        else {
            htimer_reset(keepalive_timer);
        }
    }
};

#endif // HTTP_HANDLER_H_
