#include <cstdio>
#include <cstring>
#include <string>

#include "HttpHandler.h"
#include "HttpService.h"

using namespace hv;

#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

#if defined(WITH_NGHTTP2)
#define HAVE_HTTP2 1
#else
#define HAVE_HTTP2 0
#endif

static const char* kH2CUpgradeRequest = "GET /ping HTTP/1.1\r\n"
                                       "Host: localhost\r\n"
                                       "Upgrade: h2c\r\n"
                                       "HTTP2-Settings: AAMAAABkAAQAAP__\r\n"
                                       "Connection: Upgrade, HTTP2-Settings\r\n"
                                       "\r\n";

static int probe(bool ssl) {
    HttpService router;
    router.GET("/ping", [](HttpRequest*, HttpResponse* resp) { return resp->String("pong"); });

    HttpHandler handler;
    handler.service = &router;
    handler.ssl = ssl ? 1 : 0;
    std::string request(kH2CUpgradeRequest);
    int nfeed = handler.FeedRecvData(request.data(), request.size());
    CHECK(nfeed == static_cast<int>(request.size()));

    if (HAVE_HTTP2 && !ssl) {
        CHECK(handler.protocol == HttpHandler::HTTP_V2);
    }
    else {
        CHECK(handler.protocol == HttpHandler::HTTP_V1);
        CHECK(handler.upgrade == 0);
        CHECK(handler.resp->status_code == HTTP_STATUS_OK);
    }
    return 0;
}

int main() {
    CHECK(probe(false) == 0);
    CHECK(probe(true) == 0);
    return 0;
}
