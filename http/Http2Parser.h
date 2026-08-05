#ifndef HV_HTTP2_PARSER_H_
#define HV_HTTP2_PARSER_H_

#ifdef WITH_NGHTTP2
#include "HttpParser.h"
#include "http2def.h"
#include "grpcdef.h"

#include "nghttp2/nghttp2.h"

enum http2_session_state {
    H2_WANT_RECV,

    H2_RECV_SETTINGS,
    H2_RECV_PING,
    H2_RECV_HEADERS,
    H2_RECV_DATA,
};

// HTTP/2 parser adapter over nghttp2.
//
// LIMITATION: a single active stream per session. There is one `parsed` /
// `submited` / `stream_id`, so header/data callbacks all target the same
// message regardless of frame stream_id. This matches libhv's one-request-
// per-connection HTTP usage (sync/async client, HttpHandler). True concurrent
// multiplexing (multiple in-flight streams on one connection) is NOT supported
// and would merge streams' data; do not rely on it.
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
    // outgoing body cursor for the nghttp2 data_provider read callback.
    // For gRPC, send_body holds the 5-byte-length-prefixed payload.
    const char*                     send_buf;
    size_t                          send_len;
    size_t                          send_off;
    std::string                     send_body;   // owns grpc-framed payload
    bool                            is_grpc;

    Http2Parser(http_session_type type = HTTP_CLIENT);
    virtual ~Http2Parser();

    static void initCallbacks();

    // stage submited body into the send cursor (grpc-frames it when needed)
    void prepareSendBody();

    virtual int GetSendData(char** data, size_t* len);
    virtual int FeedRecvData(const char* data, size_t len);

    virtual int GetState() {
        return (int)state;
    }

    virtual bool WantRecv() {
        return state == H2_WANT_RECV;
    }

    virtual bool WantSend() {
        // nghttp2 still has queued frames (DATA deferred by flow control,
        // SETTINGS/PING ACK, WINDOW_UPDATE, trailers, ...) to write out.
        return session && nghttp2_session_want_write(session);
    }

    virtual bool IsComplete() {
        return stream_closed && (frame_type_when_stream_closed == HTTP2_DATA ||
                                 frame_type_when_stream_closed == HTTP2_HEADERS ||
                                 frame_type_when_stream_closed == HTTP2_RST_STREAM);
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
