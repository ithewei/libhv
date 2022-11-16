#include "Http1Parser.h"

#define MAX_CONTENT_LENGTH  (1 << 24)   // 16M

static int on_url(http_parser* parser, const char *at, size_t length);
static int on_status(http_parser* parser, const char *at, size_t length);
static int on_header_field(http_parser* parser, const char *at, size_t length);
static int on_header_value(http_parser* parser, const char *at, size_t length);
static int on_body(http_parser* parser, const char *at, size_t length);
static int on_message_begin(http_parser* parser);
static int on_headers_complete(http_parser* parser);
static int on_message_complete(http_parser* parser);
static int on_chunk_header(http_parser* parser);
static int on_chunk_complete(http_parser* parser);

http_parser_settings Http1Parser::cbs = {
    on_message_begin,
    on_url,
    on_status,
    on_header_field,
    on_header_value,
    on_headers_complete,
    on_body,
    on_message_complete,
    on_chunk_header,
    on_chunk_complete
};

Http1Parser::Http1Parser(http_session_type type) {
    http_parser_init(&parser, HTTP_BOTH);
    parser.data = this;
    flags = 0;
    state = HP_START_REQ_OR_RES;
    submited = NULL;
    parsed = NULL;
}

Http1Parser::~Http1Parser() {
}

int on_url(http_parser* parser, const char *at, size_t length) {
    printd("on_url:%.*s\n", (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_URL;
    hp->url.append(at, length);
    return 0;
}

int on_status(http_parser* parser, const char *at, size_t length) {
    printd("on_status:%d %.*s\n", (int)parser->status_code, (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_STATUS;
    return 0;
}

int on_header_field(http_parser* parser, const char *at, size_t length) {
    printd("on_header_field:%.*s\n", (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->handle_header();
    hp->state = HP_HEADER_FIELD;
    hp->header_field.append(at, length);
    return 0;
}

int on_header_value(http_parser* parser, const char *at, size_t length) {
    printd("on_header_value:%.*s\n", (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_HEADER_VALUE;
    hp->header_value.append(at, length);
    return 0;
}

int on_body(http_parser* parser, const char *at, size_t length) {
    printd("on_body:%d\n", (int)length);
    // printd("on_body:%.*s\n", (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_BODY;
    if (hp->invokeHttpCb(at, length) != 0) {
        hp->parsed->body.append(at, length);
    }
    return 0;
}

int on_message_begin(http_parser* parser) {
    printd("on_message_begin\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_MESSAGE_BEGIN;
    hp->invokeHttpCb();
    return 0;
}

int on_headers_complete(http_parser* parser) {
    printd("on_headers_complete\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->handle_header();

    bool skip_body = false;
    hp->parsed->http_major = parser->http_major;
    hp->parsed->http_minor = parser->http_minor;
    if (hp->parsed->type == HTTP_REQUEST) {
        HttpRequest* req = (HttpRequest*)hp->parsed;
        req->method = (http_method)parser->method;
        req->url = hp->url;
    }
    else if (hp->parsed->type == HTTP_RESPONSE) {
        HttpResponse* res = (HttpResponse*)hp->parsed;
        res->status_code = (http_status)parser->status_code;
        // response to HEAD
        if (hp->flags & F_SKIPBODY) {
            skip_body = true;
        }
    }

    auto iter = hp->parsed->headers.find("content-type");
    if (iter != hp->parsed->headers.end()) {
        hp->parsed->content_type = http_content_type_enum(iter->second.c_str());
    }
    iter = hp->parsed->headers.find("content-length");
    if (iter != hp->parsed->headers.end()) {
        size_t content_length = atoll(iter->second.c_str());
        hp->parsed->content_length = content_length;
        size_t reserve_length = MIN(content_length + 1, MAX_CONTENT_LENGTH);
        if ((!skip_body) && reserve_length > hp->parsed->body.capacity()) {
            hp->parsed->body.reserve(reserve_length);
        }
    }
    hp->state = HP_HEADERS_COMPLETE;
    hp->invokeHttpCb();
    return skip_body ? 1 : 0;
}

int on_message_complete(http_parser* parser) {
    printd("on_message_complete\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_MESSAGE_COMPLETE;
    hp->invokeHttpCb();
    return 0;
}

int on_chunk_header(http_parser* parser) {
    printd("on_chunk_header:%llu\n", parser->content_length);
    Http1Parser* hp = (Http1Parser*)parser->data;
    int chunk_size = parser->content_length;
    int reserve_size = MIN(chunk_size + 1, MAX_CONTENT_LENGTH);
    if (reserve_size > hp->parsed->body.capacity()) {
        hp->parsed->body.reserve(reserve_size);
    }
    hp->state = HP_CHUNK_HEADER;
    hp->invokeHttpCb(NULL, chunk_size);
    return 0;
}

int on_chunk_complete(http_parser* parser) {
    printd("on_chunk_complete\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_CHUNK_COMPLETE;
    hp->invokeHttpCb();
    return 0;
}
