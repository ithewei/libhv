#ifndef HW_HTTP_PARSER_H_
#define HW_HTTP_PARSER_H_

#include "http_parser.h"
#include "HttpRequest.h"
#include "hstring.h"

enum http_parser_state {
    HP_START_REQ_OR_RES,
    HP_MESSAGE_BEGIN,
    HP_URL,
    HP_STATUS,
    HP_HEADER_FIELD,
    HP_HEADER_VALUE,
    HP_HEADERS_COMPLETE,
    HP_BODY,
    HP_MESSAGE_COMPLETE
};

struct http_parser_userdata {
    http_parser_type    type;
    http_parser_state   state;
    HttpInfo*           payload;
    // tmp
    std::string url;
    std::string header_field;
    std::string header_value;

    void handle_header() {
        if (header_field.size() != 0 && header_value.size() != 0) {
            payload->headers[header_field] = header_value;
            header_field.clear();
            header_value.clear();
        }
    }

    void init() {
        state = HP_START_REQ_OR_RES;
        url.clear();
        header_field.clear();
        header_value.clear();
    }
};

class HttpParser {
    http_parser_settings            hp_settings;
    http_parser                     hp_parser;
    http_parser_userdata            hp_userdata;

public:
    HttpParser() {
        http_parser_settings_init(&hp_settings);
        hp_settings.on_message_begin    = HttpParser::on_message_begin;
        hp_settings.on_url              = HttpParser::on_url;
        hp_settings.on_status           = HttpParser::on_status;
        hp_settings.on_header_field     = HttpParser::on_header_field;
        hp_settings.on_header_value     = HttpParser::on_header_value;
        hp_settings.on_headers_complete = HttpParser::on_headers_complete;
        hp_settings.on_body             = HttpParser::on_body;
        hp_settings.on_message_complete = HttpParser::on_message_complete;
        hp_parser.data = &hp_userdata;
    }

    void parser_request_init(HttpRequest* req) {
        hp_userdata.init();
        hp_userdata.type = HTTP_REQUEST;
        hp_userdata.payload = req;
        http_parser_init(&hp_parser, HTTP_REQUEST);
    }

    void parser_response_init(HttpResponse* res) {
        hp_userdata.init();
        hp_userdata.type = HTTP_RESPONSE;
        hp_userdata.payload = res;
        http_parser_init(&hp_parser, HTTP_RESPONSE);
    }

    int execute(const char* data, size_t len) {
        return http_parser_execute(&hp_parser, &hp_settings, data, len);
    }

    http_errno get_errno() {
        return (http_errno)hp_parser.http_errno;
    }

    http_parser_state get_state() {
        return hp_userdata.state;
    }

protected:
    static int on_url(http_parser* parser, const char *at, size_t length);
    static int on_status(http_parser* parser, const char *at, size_t length);
    static int on_header_field(http_parser* parser, const char *at, size_t length);
    static int on_header_value(http_parser* parser, const char *at, size_t length);
    static int on_body(http_parser* parser, const char *at, size_t length);
    static int on_message_begin(http_parser* parser);
    static int on_headers_complete(http_parser* parser);
    static int on_message_complete(http_parser* parser);
};

#endif // HW_HTTP_PARSER_H_
