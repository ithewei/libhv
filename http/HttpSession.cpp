#include "HttpSession.h"

#include "Http1Session.h"
#include "Http2Session.h"

HttpSession* HttpSession::New(http_session_type type, http_version version) {
    HttpSession* hs = NULL;
    if (version == HTTP_V1) {
        hs = new Http1Session(type);
    }
    else if (version == HTTP_V2) {
#ifdef WITH_NGHTTP2
        hs = new Http2Session(type);
#else
        fprintf(stderr, "Please recompile WITH_NGHTTP2!\n");
#endif
    }

    if (hs) {
        hs->version = version;
        hs->type = type;
    }

    return hs;
}
