#include "HttpParser.h"

#include "Http1Parser.h"
#include "Http2Parser.h"

HttpParser* HttpParser::New(http_session_type type, http_version version) {
    HttpParser* hp = NULL;
    if (version == HTTP_V1) {
        hp = new Http1Parser(type);
    }
    else if (version == HTTP_V2) {
#ifdef WITH_NGHTTP2
        hp = new Http2Parser(type);
#else
        fprintf(stderr, "Please recompile WITH_NGHTTP2!\n");
#endif
    }

    if (hp) {
        hp->version = version;
        hp->type = type;
    }

    return hp;
}
