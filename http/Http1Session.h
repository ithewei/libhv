#ifndef HTTP1_SESSION_H_
#define HTTP1_SESSION_H_

#include "HttpSession.h"
#include "http_parser.h"

enum http_parser_state {
    HP_START_REQ_OR_RES,
    HP_MESSAGE_BEGIN, HP_URL,
    HP_STATUS,
    HP_HEADER_FIELD,
    HP_HEADER_VALUE,
    HP_HEADERS_COMPLETE,
    HP_BODY,
    HP_MESSAGE_COMPLETE
};

class Http1Session : public HttpSession {
public:
    static http_parser_settings*    cbs;
    http_parser                     parser;
    http_parser_state               state;
    HttpPayload*                    submited;
    HttpPayload*                    parsed;
    // tmp
    std::string url;          // for on_url
    std::string header_field; // for on_header_field
    std::string header_value; // for on_header_value
    std::string sendbuf;      // for GetSendData

    Http1Session(http_session_type type = HTTP_CLIENT);
    virtual ~Http1Session();

    void handle_header() {
        if (header_field.size() != 0 && header_value.size() != 0) {
            parsed->headers[header_field] = header_value;
            header_field.clear();
            header_value.clear();
        }
    }

    virtual int GetSendData(char** data, size_t* len);
    virtual int FeedRecvData(const char* data, size_t len);
    virtual bool WantRecv();

    // client
    // SubmitRequest -> while(GetSendData) {send} -> InitResponse -> do {recv -> FeedRecvData} while(WantRecv)
    virtual int SubmitRequest(HttpRequest* req);
    virtual int InitResponse(HttpResponse* res);

    // server
    // InitRequest -> do {recv -> FeedRecvData} while(WantRecv) -> SubmitResponse -> while(GetSendData) {send}
    virtual int InitRequest(HttpRequest* req);
    virtual int SubmitResponse(HttpResponse* res);

    virtual int GetError();
    virtual const char* StrError(int error);
};

#endif // HTTP1_SESSION_H_
