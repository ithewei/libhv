#ifdef WITH_NGHTTP2

#include "Http2Parser.h"

#include <list>
#include <mutex>

static nghttp2_nv make_nv(const char* name, const char* value) {
    nghttp2_nv nv;
    nv.name = (uint8_t*)name;
    nv.value = (uint8_t*)value;
    nv.namelen = strlen(name);
    nv.valuelen = strlen(value);
    nv.flags = NGHTTP2_NV_FLAG_NONE;
    return nv;
}

static nghttp2_nv make_nv2(const char* name, const char* value,
        int namelen, int valuelen) {
    nghttp2_nv nv;
    nv.name = (uint8_t*)name;
    nv.value = (uint8_t*)value;
    nv.namelen = namelen; nv.valuelen = valuelen;
    nv.flags = NGHTTP2_NV_FLAG_NONE;
    return nv;
}

static void print_frame_hd(const nghttp2_frame_hd* hd) {
    printd("[frame] length=%d type=%x flags=%x stream_id=%d\n",
        (int)hd->length, (int)hd->type, (int)hd->flags, hd->stream_id);
}
static int on_header_callback(nghttp2_session *session,
        const nghttp2_frame *frame,
        const uint8_t *name, size_t namelen,
        const uint8_t *value, size_t valuelen,
        uint8_t flags, void *userdata);
static int on_data_chunk_recv_callback(nghttp2_session *session,
        uint8_t flags, int32_t stream_id, const uint8_t *data,
        size_t len, void *userdata);
static int on_frame_recv_callback(nghttp2_session *session,
        const nghttp2_frame *frame, void *userdata);

// data_provider read callback: copy the outgoing body to nghttp2, which does
// the DATA framing, splitting to SETTINGS_MAX_FRAME_SIZE and flow control.
// (One copy into nghttp2's buffer; the safe/simple path vs. NO_COPY framing.)
// For gRPC, send_body holds the 5-byte-length-prefixed payload.
static ssize_t data_source_read_callback(nghttp2_session *session,
        int32_t stream_id, uint8_t *buf, size_t length,
        uint32_t *data_flags, nghttp2_data_source *source, void *userdata) {
    Http2Parser* hp = (Http2Parser*)userdata;
    size_t remain = hp->send_len - hp->send_off;
    size_t n = remain < length ? remain : length;
    if (n > 0) {
        memcpy(buf, hp->send_buf + hp->send_off, n);
        hp->send_off += n;
    }
    if (hp->send_off >= hp->send_len) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        // gRPC carries the status in a trailing HEADERS frame. Keep the stream
        // open (NO_END_STREAM) and submit the trailer HERE -- nghttp2 requires
        // the trailer be submitted only after EOF+NO_END_STREAM is set on the
        // final DATA (submit_trailer may be called from this callback). Doing it
        // earlier (right after submit_response) makes nghttp2 end the stream with
        // the trailer and drop the not-yet-produced DATA.
        if (hp->is_grpc && hp->type == HTTP_SERVER) {
            *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
            // grpc-status defaults to 0 (OK); the handler may override it (and
            // add grpc-message) via resp->headers. nghttp2 copies name/value,
            // and submited outlives the stream, so c_str() is safe here.
            HttpResponse* res = (HttpResponse*)hp->submited;
            auto it = res->headers.find("grpc-status");
            const char* status = (it != res->headers.end()) ? it->second.c_str() : "0";
            nghttp2_nv nvs[2];
            int n = 0;
            nvs[n++] = make_nv("grpc-status", status);
            auto mit = res->headers.find("grpc-message");
            if (mit != res->headers.end()) {
                nvs[n++] = make_nv("grpc-message", mit->second.c_str());
            }
            nghttp2_submit_trailer(session, stream_id, nvs, n);
        }
    }
    return (ssize_t)n;
}


Http2Parser::Http2Parser(http_session_type type) {
    this->type = type;
    initCallbacks();
    if (type == HTTP_CLIENT) {
        nghttp2_session_client_new(&session, cbs, this);
    }
    else if (type == HTTP_SERVER) {
        nghttp2_session_server_new(&session, cbs, this);
    }
    state = H2_WANT_RECV;
    submited = NULL;
    parsed = NULL;
    error = 0;
    stream_id = -1;
    stream_closed = 0;
    frame_type_when_stream_closed = 0;
    send_buf = NULL;
    send_len = 0;
    send_off = 0;
    is_grpc = false;

    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
    };
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings, ARRAY_SIZE(settings));
}

Http2Parser::~Http2Parser() {
    if (session) {
        nghttp2_session_del(session);
        session = NULL;
    }
}

int Http2Parser::GetSendData(char** data, size_t* len) {
    // nghttp2 produces everything: magic, SETTINGS, HEADERS, DATA (framed and
    // flow-controlled via the data_provider), SETTINGS/PING ACK, WINDOW_UPDATE,
    // trailers. Just drain it.
    ssize_t ret = nghttp2_session_mem_send(session, (const uint8_t**)data);
    if (ret < 0) {
        error = (int)ret;
        *len = 0;
        return 0;
    }
    *len = (size_t)ret;
    printd("nghttp2_session_mem_send %zu\n", *len);
    return (int)*len;
}

int Http2Parser::FeedRecvData(const char* data, size_t len) {
    printd("nghttp2_session_mem_recv %zu\n", len);
    state = H2_WANT_RECV;
    ssize_t ret = nghttp2_session_mem_recv(session, (const uint8_t*)data, len);
    if (ret < 0) {
        // nghttp2 error (e.g. NGHTTP2_ERR_*): surface it, caller closes.
        error = (int)ret;
        return (int)ret;
    }
    if ((size_t)ret != len) {
        // mem_recv normally consumes all input; a short consume is a protocol
        // error. Record a real error code (not the byte count) for GetError().
        error = NGHTTP2_ERR_PROTO;
    }
    return (int)ret;
}

int Http2Parser::SubmitRequest(HttpRequest* req) {
    submited = req;

    req->FillContentType();
    req->FillContentLength();
    if (req->ContentType() == APPLICATION_GRPC) {
        req->method = HTTP_POST;
        req->headers["te"] = "trailers";
        req->headers["user-agent"] = "grpc-c++/1.16.0 grpc-c/6.0.0 (linux; nghttp2; hw)";
        req->headers["accept-encoding"] = "identity";
        req->headers["grpc-accept-encoding"] = "identity";
    }

    std::vector<nghttp2_nv> nvs;
    char c_str[256] = {0};
    req->ParseUrl();
    nvs.push_back(make_nv(":method", http_method_str(req->method)));
    nvs.push_back(make_nv(":path", req->path.c_str()));
    nvs.push_back(make_nv(":scheme", req->scheme.c_str()));
    if (req->port == 0 ||
        req->port == DEFAULT_HTTP_PORT ||
        req->port == DEFAULT_HTTPS_PORT) {
        nvs.push_back(make_nv(":authority", req->host.c_str()));
    }
    else {
        snprintf(c_str, sizeof(c_str), "%s:%d", req->host.c_str(), req->port);
        nvs.push_back(make_nv(":authority", c_str));
    }
    const char* value;
    // HTTP/2 requires lowercase field names. Build lowercased copies rather
    // than mutating the caller's map keys in place (which is UB and a surprise
    // side effect). std::list keeps element addresses stable for make_nv2.
    std::list<std::string> lower_names;
    for (auto& header : req->headers) {
        lower_names.push_back(header.first);
        std::string& name = lower_names.back();
        hv_strlower(&name[0]);
        value = header.second.c_str();
        if (name == "host") {
            // :authority
            continue;
        }
        if (name == "connection") {
            // HTTP2 default keep-alive
            continue;
        }
        if (name == "content-length") {
            // HTTP2 have frame_hd.length
            continue;
        }
        nvs.push_back(make_nv2(name.c_str(), value, name.size(), header.second.size()));
    }
    // Set up the outgoing body as an nghttp2 data_provider so nghttp2 handles
    // DATA framing, frame splitting and flow control. For gRPC, prepend the
    // 5-byte length-prefix into an owned buffer.
    is_grpc = (req->ContentType() == APPLICATION_GRPC);
    prepareSendBody();
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = this;
    data_prd.read_callback = data_source_read_callback;
    stream_id = nghttp2_submit_request(session, NULL, &nvs[0], nvs.size(),
                                       send_len > 0 ? &data_prd : NULL, NULL);
    if (stream_id < 0) {
        error = stream_id;
        return -1;
    }
    return 0;
}

// Stage submited->Content() into the send cursor; for gRPC prepend the
// length-prefixed message header into an owned buffer (send_body).
void Http2Parser::prepareSendBody() {
    send_off = 0;
    const char* content = (const char*)submited->Content();
    size_t content_length = submited->ContentLength();
    if (is_grpc) {
        // gRPC always carries a length-prefixed message, even for an empty
        // payload (e.g. google.protobuf.Empty). Emitting the 5-byte header also
        // keeps send_len > 0 so a data_provider is attached and the stream stays
        // open for the grpc-status trailer.
        grpc_message_hd msghd;
        msghd.flags = 0;
        msghd.length = (unsigned int)content_length;
        send_body.resize(GRPC_MESSAGE_HDLEN + content_length);
        grpc_message_hd_pack(&msghd, (unsigned char*)&send_body[0]);
        if (content_length > 0) {
            memcpy(&send_body[GRPC_MESSAGE_HDLEN], content, content_length);
        }
        send_buf = send_body.data();
        send_len = send_body.size();
    } else {
        send_buf = content;
        send_len = content_length;
    }
}

int Http2Parser::SubmitResponse(HttpResponse* res) {
    submited = res;

    res->FillContentType();
    res->FillContentLength();
    if (parsed && parsed->ContentType() == APPLICATION_GRPC) {
        // correct content_type: application/grpc
        if (res->ContentType() != APPLICATION_GRPC) {
            res->content_type = APPLICATION_GRPC;
            res->headers["content-type"] = http_content_type_str(APPLICATION_GRPC);
        }
        //res->headers["accept-encoding"] = "identity";
        //res->headers["grpc-accept-encoding"] = "identity";
        //res->headers["grpc-status"] = "0";
        //res->status_code = HTTP_STATUS_OK;
    }

    std::vector<nghttp2_nv> nvs;
    char c_str[256] = {0};
    snprintf(c_str, sizeof(c_str), "%d", res->status_code);
    nvs.push_back(make_nv(":status", c_str));
    const char* value;
    // lowercase field names without mutating the caller's map keys (see SubmitRequest).
    std::list<std::string> lower_names;
    for (auto& header : res->headers) {
        lower_names.push_back(header.first);
        std::string& name = lower_names.back();
        hv_strlower(&name[0]);
        value = header.second.c_str();
        if (name == "connection") {
            // HTTP2 default keep-alive
            continue;
        }
        if (name == "content-length") {
            // HTTP2 have frame_hd.length
            continue;
        }
        if (name == "grpc-status" || name == "grpc-message") {
            // trailer-only fields, emitted in the data_provider read callback
            continue;
        }
        nvs.push_back(make_nv2(name.c_str(), value, name.size(), header.second.size()));
    }
    if (stream_id == -1) {
        // h2c upgrade: the HTTP/1.1 request maps to stream 1.
        nghttp2_session_upgrade(session, NULL, 0, NULL);
        stream_id = 1;
    }
    // Set up the outgoing body as an nghttp2 data_provider (framing + splitting
    // + flow control handled by nghttp2). For gRPC, prepend the length-prefix.
    is_grpc = (parsed && parsed->ContentType() == APPLICATION_GRPC);
    prepareSendBody();
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = this;
    data_prd.read_callback = data_source_read_callback;
    int ret = nghttp2_submit_response(session, stream_id, &nvs[0], nvs.size(),
                                      send_len > 0 ? &data_prd : NULL);
    if (ret != 0) {
        error = ret;
        return -1;
    }
    // NOTE: for gRPC the grpc-status trailer is submitted inside the data
    // provider read callback (after EOF+NO_END_STREAM), per nghttp2's contract.
    return 0;
}

int Http2Parser::InitResponse(HttpResponse* res) {
    res->Reset();
    res->http_major = 2;
    res->http_minor = 0;
    parsed = res;
    return 0;
}

int Http2Parser::InitRequest(HttpRequest* req) {
    req->Reset();
    req->http_major = 2;
    req->http_minor = 0;
    parsed = req;
    return 0;
}

nghttp2_session_callbacks* Http2Parser::cbs = NULL;

void Http2Parser::initCallbacks() {
    // thread-safe one-time init (multi-worker servers construct concurrently).
    static std::once_flag once;
    std::call_once(once, []() {
        nghttp2_session_callbacks_new(&cbs);
        nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_callback);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_recv_callback);
        nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, on_frame_recv_callback);
    });
}

int on_header_callback(nghttp2_session *session,
    const nghttp2_frame *frame,
    const uint8_t *_name, size_t namelen,
    const uint8_t *_value, size_t valuelen,
    uint8_t flags, void *userdata) {
    printd("on_header_callback\n");
    print_frame_hd(&frame->hd);
    const char* name = (const char*)_name;
    const char* value = (const char*)_value;
    printd("%s: %s\n", name, value);
    Http2Parser* hp = (Http2Parser*)userdata;
    if (*name == ':') {
        if (hp->parsed->type == HTTP_REQUEST) {
            // :method :path :scheme :authority
            HttpRequest* req = (HttpRequest*)hp->parsed;
            if (strcmp(name, ":method") == 0) {
                req->method = http_method_enum(value);
            }
            else if (strcmp(name, ":path") == 0) {
                req->url = value;
            }
            else if (strcmp(name, ":scheme") == 0) {
                req->headers["Scheme"] = value;
            }
            else if (strcmp(name, ":authority") == 0) {
                req->headers["Host"] = value;
            }
        }
        else if (hp->parsed->type == HTTP_RESPONSE) {
            HttpResponse* res = (HttpResponse*)hp->parsed;
            if (strcmp(name, ":status") == 0) {
                res->status_code = (http_status)atoi(value);
                if (res->http_cb) {
                    res->http_cb(res, HP_MESSAGE_BEGIN, NULL, 0);
                }
            }
        }
    }
    else {
        hp->parsed->headers[name] = value;
        if (strcmp(name, "content-type") == 0) {
            hp->parsed->content_type = http_content_type_enum(value);
        }
    }
    return 0;
}

int on_data_chunk_recv_callback(nghttp2_session *session,
    uint8_t flags, int32_t stream_id, const uint8_t *data,
    size_t len, void *userdata) {
    printd("on_data_chunk_recv_callback\n");
    printd("stream_id=%d length=%d\n", stream_id, (int)len);
    //printd("%.*s\n", (int)len, data);
    Http2Parser* hp = (Http2Parser*)userdata;

    if (hp->parsed->ContentType() == APPLICATION_GRPC) {
        // grpc_message_hd
        if (len >= GRPC_MESSAGE_HDLEN) {
            grpc_message_hd msghd;
            grpc_message_hd_unpack(&msghd, data);
            printd("grpc_message_hd: flags=%d length=%d\n", msghd.flags, msghd.length);
            data += GRPC_MESSAGE_HDLEN;
            len -= GRPC_MESSAGE_HDLEN;
            //printd("%.*s\n", (int)len, data);
        }
    }
    if (hp->parsed->http_cb) {
        hp->parsed->http_cb(hp->parsed, HP_BODY, (const char*)data, len);
    } else {
        hp->parsed->body.append((const char*)data, len);
    }
    return 0;
}

int on_frame_recv_callback(nghttp2_session *session,
    const nghttp2_frame *frame, void *userdata) {
    printd("on_frame_recv_callback\n");
    print_frame_hd(&frame->hd);
    Http2Parser* hp = (Http2Parser*)userdata;
    switch (frame->hd.type) {
    case NGHTTP2_DATA:
        hp->state = H2_RECV_DATA;
        break;
    case NGHTTP2_HEADERS:
        hp->state = H2_RECV_HEADERS;
        break;
    case NGHTTP2_SETTINGS:
        hp->state = H2_RECV_SETTINGS;
        break;
    case NGHTTP2_PING:
        hp->state = H2_RECV_PING;
        break;
    case NGHTTP2_RST_STREAM:
        // peer aborted the stream: surface the error and mark complete so the
        // recv loop / IsComplete() doesn't hang waiting for END_STREAM.
        hp->error = (int)frame->rst_stream.error_code;
        hp->stream_closed = 1;
        hp->frame_type_when_stream_closed = HTTP2_RST_STREAM;
        if (hp->parsed && hp->parsed->http_cb) {
            hp->parsed->http_cb(hp->parsed, HP_MESSAGE_COMPLETE, NULL, 0);
        }
        return 0;
    case NGHTTP2_GOAWAY:
        // peer is closing the connection; record it (nghttp2 tracks state).
        hp->error = (int)frame->goaway.error_code;
        return 0;
    case NGHTTP2_WINDOW_UPDATE:
        // flow-control window opened; deferred DATA will be sent on next
        // GetSendData drain. Nothing to do here.
        return 0;
    default:
        break;
    }
    if (hp->state == H2_RECV_HEADERS && hp->parsed->http_cb) {
        hp->parsed->http_cb(hp->parsed, HP_HEADERS_COMPLETE, NULL, 0);
    }
    if (frame->hd.stream_id >= hp->stream_id) {
        hp->stream_id = frame->hd.stream_id;
        hp->stream_closed = 0;
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            printd("on_stream_closed stream_id=%d\n", hp->stream_id);
            hp->stream_closed = 1;
            hp->frame_type_when_stream_closed = frame->hd.type;
            if (hp->state == H2_RECV_HEADERS || hp->state == H2_RECV_DATA) {
                if (hp->parsed->http_cb) {
                    hp->parsed->http_cb(hp->parsed, HP_MESSAGE_COMPLETE, NULL, 0);
                }
            }
        }
    }

    return 0;
}

#endif
