#include "Http1Parser.h"

static int on_url(http_parser* parser, const char *at, size_t length);
static int on_status(http_parser* parser, const char *at, size_t length);
static int on_header_field(http_parser* parser, const char *at, size_t length);
static int on_header_value(http_parser* parser, const char *at, size_t length);
static int on_body(http_parser* parser, const char *at, size_t length);
static int on_message_begin(http_parser* parser);
static int on_headers_complete(http_parser* parser);
static int on_message_complete(http_parser* parser);

http_parser_settings* Http1Parser::cbs = NULL;

Http1Parser::Http1Parser(http_session_type type) {
    if (cbs == NULL) {
        cbs = (http_parser_settings*)malloc(sizeof(http_parser_settings));
        http_parser_settings_init(cbs);
        cbs->on_message_begin    = on_message_begin;
        cbs->on_url              = on_url;
        cbs->on_status           = on_status;
        cbs->on_header_field     = on_header_field;
        cbs->on_header_value     = on_header_value;
        cbs->on_headers_complete = on_headers_complete;
        cbs->on_body             = on_body;
        cbs->on_message_complete = on_message_complete;
    }
    http_parser_init(&parser, HTTP_BOTH);
    parser.data = this;
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
    printd("on_status:%.*s\n", (int)length, at);
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
    printd("on_header_value:%.*s""\n", (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_HEADER_VALUE;
    hp->header_value.append(at, length);
    return 0;
}

int on_body(http_parser* parser, const char *at, size_t length) {
    //printd("on_body:%.*s""\n", (int)length, at);
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_BODY;
    hp->parsed->body.append(at, length);
    return 0;
}

int on_message_begin(http_parser* parser) {
    printd("on_message_begin\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_MESSAGE_BEGIN;
    return 0;
}

int on_headers_complete(http_parser* parser) {
    printd("on_headers_complete\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->handle_header();
    auto iter = hp->parsed->headers.find("content-type");
    if (iter != hp->parsed->headers.end()) {
        hp->parsed->content_type = http_content_type_enum(iter->second.c_str());
    }
    iter = hp->parsed->headers.find("content-length");
    if (iter != hp->parsed->headers.end()) {
        int content_length = atoi(iter->second.c_str());
        hp->parsed->content_length = content_length;
        if (content_length > hp->parsed->body.capacity()) {
            hp->parsed->body.reserve(content_length);
        }
    }
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
    }
    hp->state = HP_HEADERS_COMPLETE;
    return 0;
}

int on_message_complete(http_parser* parser) {
    printd("on_message_complete\n");
    Http1Parser* hp = (Http1Parser*)parser->data;
    hp->state = HP_MESSAGE_COMPLETE;
    return 0;
}

