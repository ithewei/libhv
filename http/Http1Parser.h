#ifndef HV_HTTP1_PARSER_H_
#define HV_HTTP1_PARSER_H_

#include "HttpParser.h"
#include "http_parser.h"

class Http1Parser : public HttpParser {
public:
    static http_parser_settings     cbs;
    http_parser                     parser;
    int                             flags;
    http_parser_state               state;
    HttpMessage*                    submited;
    HttpMessage*                    parsed;
    // tmp
    std::string url;          // for on_url
    std::string header_field; // for on_header_field
    std::string header_value; // for on_header_value
    std::string sendbuf;      // for GetSendData

    Http1Parser(http_session_type type = HTTP_CLIENT);
    virtual ~Http1Parser();

    void handle_header() {
        if (header_field.size() != 0) {
            if (stricmp(header_field.c_str(), "Set-CooKie") == 0 ||
                stricmp(header_field.c_str(), "Cookie") == 0) {
                HttpCookie cookie;
                if (cookie.parse(header_value)) {
                    parsed->cookies.emplace_back(cookie);
                    header_field.clear();
                    header_value.clear();
                    return;
                }
            }
            parsed->headers[header_field] = header_value;
            header_field.clear();
            header_value.clear();
        }
    }

    virtual int GetSendData(char** data, size_t* len) {
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

    virtual int FeedRecvData(const char* data, size_t len) {
        return http_parser_execute(&parser, &cbs, data, len);
    }

    virtual int  GetState() {
        return (int)state;
    }

    virtual bool WantRecv() {
        return state != HP_MESSAGE_COMPLETE;
    }

    virtual bool WantSend() {
        return state == HP_MESSAGE_COMPLETE;
    }

    virtual bool IsComplete() {
        return state == HP_MESSAGE_COMPLETE;
    }

    virtual bool IsEof() {
        FeedRecvData(NULL, 0);
        return IsComplete();
    }

    virtual int GetError() {
        return parser.http_errno;
    }

    virtual const char* StrError(int error) {
        return http_errno_description((enum http_errno)error);
    }

    // client
    // SubmitRequest -> while(GetSendData) {send} -> InitResponse -> do {recv -> FeedRecvData} while(WantRecv)
    virtual int SubmitRequest(HttpRequest* req) {
        submited = req;
        if (req) {
            if (req->method == HTTP_HEAD) {
                flags |= F_SKIPBODY;
            } else {
                flags &= ~F_SKIPBODY;
            }
        }
        return 0;
    }

    virtual int InitResponse(HttpResponse* res) {
        res->Reset();
        parsed = res;
        http_parser_init(&parser, HTTP_RESPONSE);
        url.clear();
        header_field.clear();
        header_value.clear();
        return 0;
    }

    // server
    // InitRequest -> do {recv -> FeedRecvData} while(WantRecv) -> SubmitResponse -> while(GetSendData) {send}
    virtual int InitRequest(HttpRequest* req) {
        req->Reset();
        parsed = req;
        http_parser_init(&parser, HTTP_REQUEST);
        state = HP_START_REQ_OR_RES;
        url.clear();
        header_field.clear();
        header_value.clear();
        return 0;
    }

    virtual int SubmitResponse(HttpResponse* res) {
        submited = res;
        return 0;
    }

    // HttpMessage::http_cb
    int invokeHttpCb(const char* data = NULL, size_t size = 0) {
        if (parsed->http_cb == NULL) return -1;
        parsed->http_cb(parsed, state, data, size);
        return 0;
    }
};

#endif // HV_HTTP1_PARSER_H_
