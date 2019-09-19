#ifndef HTTP_SESSION_H_
#define HTTP_SESSION_H_

#include "HttpPayload.h"

class HttpSession {
public:
    static HttpSession* New(http_session_type type = HTTP_CLIENT, http_version version = HTTP_V1);
    virtual ~HttpSession() {}

    virtual int GetSendData(char** data, size_t* len) = 0;
    virtual int FeedRecvData(const char* data, size_t len) = 0;
    virtual bool WantRecv() = 0;

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
