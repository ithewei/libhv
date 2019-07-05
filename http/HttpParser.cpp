#include "HttpParser.h"

int HttpParser::on_url(http_parser* parser, const char *at, size_t length) {
    //printf("on_url:%.*s\n", (int)length, at);
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->state = HP_URL;
    userdata->url.insert(userdata->url.size(), at, length);
    return 0;
}

int HttpParser::on_status(http_parser* parser, const char *at, size_t length) {
    //printf("on_status:%.*s\n", (int)length, at);
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->state = HP_STATUS;
    return 0;
}

int HttpParser::on_header_field(http_parser* parser, const char *at, size_t length) {
    //printf("on_header_field:%.*s\n", (int)length, at);
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->handle_header();
    userdata->state = HP_HEADER_FIELD;
    userdata->header_field.insert(userdata->header_field.size(), at, length);
    return 0;
}

int HttpParser::on_header_value(http_parser* parser, const char *at, size_t length) {
    //printf("on_header_value:%.*s""\n", (int)length, at);
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->state = HP_HEADER_VALUE;
    userdata->header_value.insert(userdata->header_value.size(), at, length);
    return 0;
}

int HttpParser::on_body(http_parser* parser, const char *at, size_t length) {
    //printf("on_body:%.*s""\n", (int)length, at);
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->state = HP_BODY;
    userdata->payload->body.insert(userdata->payload->body.size(), at, length);
    return 0;
}

int HttpParser::on_message_begin(http_parser* parser) {
    //printf("on_message_begin\n");
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->state = HP_MESSAGE_BEGIN;
    return 0;
}

int HttpParser::on_headers_complete(http_parser* parser) {
    //printf("on_headers_complete\n");
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->handle_header();
    auto iter = userdata->payload->headers.find("content-type");
    if (iter != userdata->payload->headers.end()) {
        userdata->payload->content_type = http_content_type_enum(iter->second.c_str());
    }
    userdata->payload->http_major = parser->http_major;
    userdata->payload->http_minor = parser->http_minor;
    if (userdata->type == HTTP_REQUEST) {
        HttpRequest* req = (HttpRequest*)userdata->payload;
        req->method = (http_method)parser->method;
        req->url = userdata->url;
    }
    else if (userdata->type == HTTP_RESPONSE) {
        HttpResponse* res = (HttpResponse*)userdata->payload;
        res->status_code = (http_status)parser->status_code;
    }
    userdata->state = HP_HEADERS_COMPLETE;
    return 0;
}

int HttpParser::on_message_complete(http_parser* parser) {
    //printf("on_message_complete\n");
    http_parser_userdata* userdata = (http_parser_userdata*)parser->data;
    userdata->state = HP_MESSAGE_COMPLETE;
    return 0;
}

