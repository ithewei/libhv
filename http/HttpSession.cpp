#include "HttpSession.h"

#include "Http1Session.h"
#include "Http2Session.h"

HttpSession* HttpSession::New(http_session_type type, http_version version) {
    if (version == HTTP_V1) {
        return new Http1Session(type);
    }
    else if (version == HTTP_V2) {
#ifdef WITH_NGHTTP2
        return new Http2Session(type);
#else
        fprintf(stderr, "Please recompile WITH_NGHTTP2!\n");
#endif
    }

    return NULL;
}
