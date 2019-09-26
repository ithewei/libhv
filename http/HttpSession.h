#ifndef HTTP_SESSION_H_
#define HTTP_SESSION_H_

#include "HttpPayload.h"

class HttpSession {
public:
    http_version        version;
    http_session_type   type;

    static HttpSession* New(http_session_type type = HTTP_CLIENT, http_version version = HTTP_V1);
    virtual ~HttpSession() {}

    virtual int GetSendData(char** data, size_t* len) = 0;
    virtual int FeedRecvData(const char* data, size_t len) = 0;

    // Http1Session: http_parser_state
    // Http2Session: http2_session_state
    virtual int GetState() = 0;

    // Http1Session: GetState() != HP_MESSAGE_COMPLETE
    // Http2Session: GetState() == HSS_WANT_RECV
    virtual bool WantRecv() = 0;

    // Http1Session: GetState() == HP_MESSAGE_COMPLETE
    // Http2Session: GetState() == HSS_WANT_SEND
    virtual bool WantSend() = 0;

    // IsComplete: Is recved HttpRequest or HttpResponse complete?
    // Http1Session: GetState() == HP_MESSAGE_COMPLETE
    // Http2Session: (state == HSS_RECV_HEADERS || state == HSS_RECV_DATA) && stream_closed
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

#endif // HTTP_SESSION_H_
