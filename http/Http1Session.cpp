#include "Http1Session.h"

static int on_url(http_parser* parser, const char *at, size_t length);
static int on_status(http_parser* parser, const char *at, size_t length);
static int on_header_field(http_parser* parser, const char *at, size_t length);
static int on_header_value(http_parser* parser, const char *at, size_t length);
static int on_body(http_parser* parser, const char *at, size_t length);
static int on_message_begin(http_parser* parser);
static int on_headers_complete(http_parser* parser);
static int on_message_complete(http_parser* parser);

Http1Session::Http1Session(http_session_type type) {
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

Http1Session::~Http1Session() {
}

int Http1Session::GetSendData(char** data, size_t* len) {
    if (!submited) {
        *data = NULL;
        *len = 0;
        return 0;
    }
    sendbuf = submited->Dump(true, true);
    submited = NULL;
    *data = (char*)sendbuf.data();
    *len = sendbuf.size();
    return sendbuf.size();
}

int Http1Session::FeedRecvData(const char* data, size_t len) {
    return http_parser_execute(&parser, cbs, data, len);
}

bool Http1Session::WantRecv() {
    return state != HP_MESSAGE_COMPLETE;
}

int Http1Session::SubmitRequest(HttpRequest* req) {
    submited = req;
    return 0;
}

int Http1Session::SubmitResponse(HttpResponse* res) {
    submited = res;
    return 0;
}

int Http1Session::InitRequest(HttpRequest* req) {
    req->Reset();
    parsed = req;
    http_parser_init(&parser, HTTP_REQUEST);
    return 0;
}

int Http1Session::InitResponse(HttpResponse* res) {
    res->Reset();
    parsed = res;
    http_parser_init(&parser, HTTP_RESPONSE);
    return 0;
}

int Http1Session::GetError() {
    return parser.http_errno;
}

const char* Http1Session::StrError(int error) {
    return http_errno_description((enum http_errno)error);
}

http_parser_settings* Http1Session::cbs = NULL;

int on_url(http_parser* parser, const char *at, size_t length) {
    printd("on_url:%.*s\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_URL;
    hss->url.insert(hss->url.size(), at, length);
    return 0;
}

int on_status(http_parser* parser, const char *at, size_t length) {
    printd("on_status:%.*s\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_STATUS;
    return 0;
}

int on_header_field(http_parser* parser, const char *at, size_t length) {
    printd("on_header_field:%.*s\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->handle_header();
    hss->state = HP_HEADER_FIELD;
    hss->header_field.insert(hss->header_field.size(), at, length);
    return 0;
}

int on_header_value(http_parser* parser, const char *at, size_t length) {
    printd("on_header_value:%.*s""\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_HEADER_VALUE;
    hss->header_value.insert(hss->header_value.size(), at, length);
    return 0;
}

int on_body(http_parser* parser, const char *at, size_t length) {
    //printd("on_body:%.*s""\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_BODY;
    hss->parsed->body.insert(hss->parsed->body.size(), at, length);
    return 0;
}

int on_message_begin(http_parser* parser) {
    printd("on_message_begin\n");
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_MESSAGE_BEGIN;
    return 0;
}

int on_headers_complete(http_parser* parser) {
    printd("on_headers_complete\n");
    Http1Session* hss = (Http1Session*)parser->data;
    hss->handle_header();
    auto iter = hss->parsed->headers.find("content-type");
    if (iter != hss->parsed->headers.end()) {
        hss->parsed->content_type = http_content_type_enum(iter->second.c_str());
    }
    hss->parsed->http_major = parser->http_major;
    hss->parsed->http_minor = parser->http_minor;
    if (hss->parsed->type == HTTP_REQUEST) {
        HttpRequest* req = (HttpRequest*)hss->parsed;
        req->method = (http_method)parser->method;
        req->url = hss->url;
    }
    else if (hss->parsed->type == HTTP_RESPONSE) {
        HttpResponse* res = (HttpResponse*)hss->parsed;
        res->status_code = (http_status)parser->status_code;
    }
    hss->state = HP_HEADERS_COMPLETE;
    return 0;
}

int on_message_complete(http_parser* parser) {
    printd("on_message_complete\n");
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_MESSAGE_COMPLETE;
    return 0;
}

