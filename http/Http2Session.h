#ifndef HTTP2_SESSION_H_
#define HTTP2_SESSION_H_

#ifdef WITH_NGHTTP2
#include "HttpSession.h"
#include "http2def.h"
#include "grpcdef.h"

#include "nghttp2/nghttp2.h"

enum http2_session_state {
    HSS_SEND_MAGIC,
    HSS_SEND_SETTINGS,
    HSS_SEND_PING,
    HSS_SEND_HEADERS,
    HSS_SEND_DATA_FRAME_HD,
    HSS_SEND_DATA,
    HSS_SEND_DONE,

    HSS_WANT_RECV,
    HSS_RECV_SETTINGS,
    HSS_RECV_PING,
    HSS_RECV_HEADERS,
    HSS_RECV_DATA,
};

class Http2Session : public HttpSession {
public:
    static nghttp2_session_callbacks* cbs;
    nghttp2_session*                session;
    http2_session_state             state;
    HttpMessage*                    submited;
    HttpMessage*                    parsed;
    int error;
    int stream_id;
    int stream_closed;
    // http2_frame_hd + grpc_message_hd
    // at least HTTP2_FRAME_HDLEN + GRPC_MESSAGE_HDLEN = 9 + 5 = 14
    unsigned char                   frame_hdbuf[32];

    Http2Session(http_session_type type = HTTP_CLIENT);
    virtual ~Http2Session();

    virtual int GetSendData(char** data, size_t* len);
    virtual int FeedRecvData(const char* data, size_t len);

    virtual int GetState() {
        return (int)state;
    }

    virtual bool WantRecv() {
        return state == HSS_WANT_RECV;
    }

    virtual bool WantSend() {
        return state != HSS_WANT_RECV;
    }

    virtual bool IsComplete() {
        // HTTP2_HEADERS / HTTP2_DATA EOS
        return (state == HSS_RECV_HEADERS || state == HSS_RECV_DATA) && stream_closed == 1;
    }

    virtual int GetError() {
        return error;
    }

    virtual const char* StrError(int error) {
        return nghttp2_http2_strerror(error);
    }

    // client
    // SubmitRequest -> while(GetSendData) {send} -> InitResponse -> do {recv -> FeedRecvData} while(WantRecv)
    virtual int SubmitRequest(HttpRequest* req);
    virtual int InitResponse(HttpResponse* res);

    // server
    // InitRequest -> do {recv -> FeedRecvData} while(WantRecv) -> SubmitResponse -> while(GetSendData) {send}
    virtual int InitRequest(HttpRequest* req);
    virtual int SubmitResponse(HttpResponse* res);

};

#endif

#endif // HTTP2_SESSION_H_
