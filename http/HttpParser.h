#ifndef HV_HTTP_PARSER_H_
#define HV_HTTP_PARSER_H_

#include "hexport.h"
#include "HttpMessage.h"

class HV_EXPORT HttpParser {
public:
    http_version        version;
    http_session_type   type;

    static HttpParser* New(http_session_type type = HTTP_CLIENT, http_version version = HTTP_V1);
    virtual ~HttpParser() {}

    virtual int GetSendData(char** data, size_t* len) = 0;
    virtual int FeedRecvData(const char* data, size_t len) = 0;

    // Http1Parser: http_parser_state
    // Http2Parser: http2_session_state
    virtual int GetState() = 0;

    // Http1Parser: GetState() != HP_MESSAGE_COMPLETE
    // Http2Parser: GetState() == H2_WANT_RECV
    virtual bool WantRecv() = 0;

    // Http1Parser: GetState() == HP_MESSAGE_COMPLETE
    // Http2Parser: GetState() == H2_WANT_SEND
    virtual bool WantSend() = 0;

    // IsComplete: Is recved HttpRequest or HttpResponse complete?
    // Http1Parser: GetState() == HP_MESSAGE_COMPLETE
    // Http2Parser: (state == H2_RECV_HEADERS || state == H2_RECV_DATA) && stream_closed
    virtual bool IsComplete() = 0;

    // client
    // SubmitRequest -> while(GetSendData) {send} -> InitResponse -> do {recv -> FeedRecvData} while(WantRecv)
    virtual int SubmitRequest(HttpRequest* req) = 0;
    virtual int InitResponse(HttpResponse* res) = 0;

    // server
    // InitRequest -> do {recv -> FeedRecvData} while(WantRecv) -> SubmitResponse -> while(GetSendData) {send}
    virtual int InitRequest(HttpRequest* req) = 0;
    virtual int SubmitResponse(HttpResponse* res) = 0;

    virtual int GetError() = 0;
    virtual const char* StrError(int error) = 0;
};

typedef std::shared_ptr<HttpParser> HttpParserPtr;

#endif // HV_HTTP_PARSER_H_
