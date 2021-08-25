#ifdef WITH_NGHTTP2

#include "Http2Parser.h"

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
/*
static ssize_t data_source_read_callback(nghttp2_session *session,
        int32_t stream_id, uint8_t *buf, size_t length,
        uint32_t *data_flags, nghttp2_data_source *source, void *userdata);
*/


Http2Parser::Http2Parser(http_session_type type) {
    if (cbs == NULL) {
        nghttp2_session_callbacks_new(&cbs);
        nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_callback);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_recv_callback);
        nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, on_frame_recv_callback);
    }
    if (type == HTTP_CLIENT) {
        nghttp2_session_client_new(&session, cbs, this);
        state = H2_SEND_MAGIC;
    }
    else if (type == HTTP_SERVER) {
        nghttp2_session_server_new(&session, cbs, this);
        state = H2_WANT_RECV;
    }
    //nghttp2_session_set_user_data(session, this);
    submited = NULL;
    parsed = NULL;
    stream_id = -1;
    stream_closed = 0;

    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
    };
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings, ARRAY_SIZE(settings));
    state = H2_SEND_SETTINGS;

    //nghttp2_submit_ping(session, NGHTTP2_FLAG_NONE, NULL);
    //state = H2_SEND_PING;
}

Http2Parser::~Http2Parser() {
    if (session) {
        nghttp2_session_del(session);
        session = NULL;
    }
}

int Http2Parser::GetSendData(char** data, size_t* len) {
    // HTTP2_MAGIC,HTTP2_SETTINGS,HTTP2_HEADERS
    *len = nghttp2_session_mem_send(session, (const uint8_t**)data);
    printd("nghttp2_session_mem_send %d\n", *len);
    if (*len != 0) return *len;

    if (submited == NULL) return 0;
    // HTTP2_DATA
    if (state == H2_SEND_HEADERS) {
        void* content = submited->Content();
        int content_length = submited->ContentLength();
        // HTTP2 DATA framehd
        state = H2_SEND_DATA_FRAME_HD;
        http2_frame_hd  framehd;
        framehd.length = content_length;
        framehd.type = HTTP2_DATA;
        framehd.flags = HTTP2_FLAG_END_STREAM;
        framehd.stream_id = stream_id;
        *data = (char*)frame_hdbuf;
        *len = HTTP2_FRAME_HDLEN;
        printd("HTTP2 SEND_DATA_FRAME_HD...\n");
        if (submited->ContentType() == APPLICATION_GRPC) {
            grpc_message_hd msghd;
            msghd.flags = 0;
            msghd.length = content_length;
            printd("grpc_message_hd: flags=%d length=%d\n", msghd.flags, msghd.length);

            if (type == HTTP_SERVER) {
                // grpc server send grpc-status in HTTP2 header frame
                framehd.flags = HTTP2_FLAG_NONE;

                /*
                // @test protobuf
                // message StringMessage {
                //     string str = 1;
                // }
                int protobuf_taglen = 0;
                int tag = PROTOBUF_MAKE_TAG(1, WIRE_TYPE_LENGTH_DELIMITED);
                unsigned char* p = frame_hdbuf + HTTP2_FRAME_HDLEN + GRPC_MESSAGE_HDLEN;
                int bytes = varint_encode(tag, p);
                protobuf_taglen += bytes;
                p += bytes;
                bytes = varint_encode(content_length, p);
                protobuf_taglen += bytes;
                msghd.length += protobuf_taglen;
                framehd.length += protobuf_taglen;
                *len += protobuf_taglen;
                */
            }

            grpc_message_hd_pack(&msghd, frame_hdbuf + HTTP2_FRAME_HDLEN);
            framehd.length += GRPC_MESSAGE_HDLEN;
            *len += GRPC_MESSAGE_HDLEN;
        }
        http2_frame_hd_pack(&framehd, frame_hdbuf);
    }
    else if (state == H2_SEND_DATA_FRAME_HD) {
        // HTTP2 DATA
        void* content = submited->Content();
        int content_length = submited->ContentLength();
        if (content_length == 0) {
            // skip send_data
            goto send_done;
        }
        else {
            printd("HTTP2 SEND_DATA... content_length=%d\n", content_length);
            state = H2_SEND_DATA;
            *data = (char*)content;
            *len = content_length;
        }
    }
    else if (state == H2_SEND_DATA) {
send_done:
        state = H2_SEND_DONE;
        if (submited->ContentType() == APPLICATION_GRPC) {
            if (type == HTTP_SERVER && stream_closed) {
                // grpc HEADERS grpc-status
                printd("grpc HEADERS grpc-status: 0\n");
                int flags = NGHTTP2_FLAG_END_STREAM | NGHTTP2_FLAG_END_HEADERS;
                nghttp2_nv nv = make_nv("grpc-status", "0");
                nghttp2_submit_headers(session, flags, stream_id, NULL, &nv, 1, NULL);
                *len = nghttp2_session_mem_send(session, (const uint8_t**)data);
            }
        }
    }

    printd("GetSendData %d\n", *len);
    return *len;
}

int Http2Parser::FeedRecvData(const char* data, size_t len) {
    printd("nghttp2_session_mem_recv %d\n", len);
    state = H2_WANT_RECV;
    size_t ret = nghttp2_session_mem_recv(session, (const uint8_t*)data, len);
    if (ret != len) {
        error = ret;
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
    const char* name;
    const char* value;
    for (auto& header : req->headers) {
        name = header.first.c_str();
        value = header.second.c_str();
        strlower((char*)name);
        if (strcmp(name, "connection") == 0) {
            // HTTP2 default keep-alive
            continue;
        }
        if (strcmp(name, "content-length") == 0) {
            // HTTP2 have frame_hd.length
            continue;
        }
        nvs.push_back(make_nv2(name, value, header.first.size(), header.second.size()));
    }
    int flags = NGHTTP2_FLAG_END_HEADERS;
    // we set EOS on DATA frame
    stream_id = nghttp2_submit_headers(session, flags, -1, NULL, &nvs[0], nvs.size(), NULL);
    // avoid DATA_SOURCE_COPY, we do not use nghttp2_submit_data
    // nghttp2_data_provider data_prd;
    // data_prd.read_callback = data_source_read_callback;
    //stream_id = nghttp2_submit_request(session, NULL, &nvs[0], nvs.size(), &data_prd, NULL);
    state = H2_SEND_HEADERS;
    return 0;
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
    const char* name;
    const char* value;
    for (auto& header : res->headers) {
        name = header.first.c_str();
        value = header.second.c_str();
        strlower((char*)name);
        if (strcmp(name, "connection") == 0) {
            // HTTP2 default keep-alive
            continue;
        }
        if (strcmp(name, "content-length") == 0) {
            // HTTP2 have frame_hd.length
            continue;
        }
        nvs.push_back(make_nv2(name, value, header.first.size(), header.second.size()));
    }
    int flags = NGHTTP2_FLAG_END_HEADERS;
    // we set EOS on DATA frame
    if (stream_id == -1) {
        // upgrade
        nghttp2_session_upgrade(session, NULL, 0, NULL);
        stream_id = 1;
    }
    nghttp2_submit_headers(session, flags, stream_id, NULL, &nvs[0], nvs.size(), NULL);
    // avoid DATA_SOURCE_COPY, we do not use nghttp2_submit_data
    // data_prd.read_callback = data_source_read_callback;
    //nghttp2_submit_response(session, stream_id, &nvs[0], nvs.size(), &data_prd);
    state = H2_SEND_HEADERS;
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
    hp->parsed->body.append((const char*)data, len);
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
    case NGHTTP2_WINDOW_UPDATE:
        // ignore
        return 0;
    default:
        break;
    }
    if (frame->hd.stream_id >= hp->stream_id) {
        hp->stream_id = frame->hd.stream_id;
        hp->stream_closed = 0;
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            printd("on_stream_closed stream_id=%d\n", hp->stream_id);
            hp->stream_closed = 1;
            hp->frame_type_when_stream_closed = frame->hd.type;
        }
    }

    return 0;
}

#endif
