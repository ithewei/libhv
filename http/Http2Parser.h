#ifndef HV_HTTP2_PARSER_H_
#define HV_HTTP2_PARSER_H_

#ifdef WITH_NGHTTP2
#include "HttpParser.h"
#include "http2def.h"
#include "grpcdef.h"

#include "nghttp2/nghttp2.h"

enum http2_session_state {
    H2_SEND_MAGIC,
    H2_SEND_SETTINGS,
    H2_SEND_PING,
    H2_SEND_HEADERS,
    H2_SEND_DATA_FRAME_HD,
    H2_SEND_DATA,
    H2_SEND_DONE,

    H2_WANT_SEND,
    H2_WANT_RECV,

    H2_RECV_SETTINGS,
    H2_RECV_PING,
    H2_RECV_HEADERS,
    H2_RECV_DATA,
};

class Http2Parser : public HttpParser {
public:
    static nghttp2_session_callbacks* cbs;
    nghttp2_session*                session;
    http2_session_state             state;
    HttpMessage*                    submited;
    HttpMessage*                    parsed;
    int error;
    int stream_id;
    int stream_closed;
    int frame_type_when_stream_closed;
    // http2_frame_hd + grpc_message_hd
    // at least HTTP2_FRAME_HDLEN + GRPC_MESSAGE_HDLEN = 9 + 5 = 14
    unsigned char                   frame_hdbuf[32];

    Http2Parser(http_session_type type = HTTP_CLIENT);
    virtual ~Http2Parser();

    virtual int GetSendData(char** data, size_t* len);
    virtual int FeedRecvData(const char* data, size_t len);

    virtual int GetState() {
        return (int)state;
    }

    virtual bool WantRecv() {
        return state == H2_WANT_RECV;
    }

    virtual bool WantSend() {
        return state <= H2_WANT_SEND;
    }

    virtual bool IsComplete() {
        return stream_closed && (frame_type_when_stream_closed == HTTP2_DATA || frame_type_when_stream_closed == HTTP2_HEADERS);
    }

    virtual int GetError() {
        return error;
    }

    virtual const char* StrError(int error) {
        //return nghttp2_http2_strerror(error);
        return nghttp2_strerror(error);
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

#endif // HV_HTTP2_PARSER_H_
