#include "HttpSession.h"

#include "http_parser.h"
static int on_url(http_parser* parser, const char *at, size_t length);
static int on_status(http_parser* parser, const char *at, size_t length);
static int on_header_field(http_parser* parser, const char *at, size_t length);
static int on_header_value(http_parser* parser, const char *at, size_t length);
static int on_body(http_parser* parser, const char *at, size_t length);
static int on_message_begin(http_parser* parser);
static int on_headers_complete(http_parser* parser);
static int on_message_complete(http_parser* parser);

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

    void handle_header() {
        if (header_field.size() != 0 && header_value.size() != 0) {
            parsed->headers[header_field] = header_value;
            header_field.clear();
            header_value.clear();
        }
    }

    Http1Session() {
        if (cbs == NULL) {
            cbs = (http_parser_settings*)malloc(sizeof(http_parser_settings));
            http_parser_settings_init(cbs);
            cbs->on_message_begin    = on_message_begin;
            cbs->on_url              = on_url;
            cbs->on_status           = on_status;
            cbs->on_header_field     = on_header_field;
            cbs->on_header_value     = on_header_value;
            cbs->on_headers_complete = on_headers_complete;
            cbs->on_body             = on_body;
            cbs->on_message_complete = on_message_complete;
        }
        http_parser_init(&parser, HTTP_BOTH);
        parser.data = this;
        state = HP_START_REQ_OR_RES;
        submited = NULL;
        parsed = NULL;
    }

    virtual ~Http1Session() {
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
        return http_parser_execute(&parser, cbs, data, len);
    }

    virtual bool WantRecv() {
        return state != HP_MESSAGE_COMPLETE;
    }

    virtual int SubmitRequest(HttpRequest* req) {
        submited = req;
        return 0;
    }

    virtual int SubmitResponse(HttpResponse* res) {
        submited = res;
        return 0;
    }

    virtual int InitRequest(HttpRequest* req) {
        req->Reset();
        parsed = req;
        http_parser_init(&parser, HTTP_REQUEST);
        return 0;
    }

    virtual int InitResponse(HttpResponse* res) {
        res->Reset();
        parsed = res;
        http_parser_init(&parser, HTTP_RESPONSE);
        return 0;
    }

    virtual int GetError() {
        return parser.http_errno;
    }

    virtual const char* StrError(int error) {
        return http_errno_description((enum http_errno)error);
    }
};

http_parser_settings* Http1Session::cbs = NULL;

int on_url(http_parser* parser, const char *at, size_t length) {
    printd("on_url:%.*s\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_URL;
    hss->url.insert(hss->url.size(), at, length);
    return 0;
}

int on_status(http_parser* parser, const char *at, size_t length) {
    printd("on_status:%.*s\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_STATUS;
    return 0;
}

int on_header_field(http_parser* parser, const char *at, size_t length) {
    printd("on_header_field:%.*s\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->handle_header();
    hss->state = HP_HEADER_FIELD;
    hss->header_field.insert(hss->header_field.size(), at, length);
    return 0;
}

int on_header_value(http_parser* parser, const char *at, size_t length) {
    printd("on_header_value:%.*s""\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_HEADER_VALUE;
    hss->header_value.insert(hss->header_value.size(), at, length);
    return 0;
}

int on_body(http_parser* parser, const char *at, size_t length) {
    //printd("on_body:%.*s""\n", (int)length, at);
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_BODY;
    hss->parsed->body.insert(hss->parsed->body.size(), at, length);
    return 0;
}

int on_message_begin(http_parser* parser) {
    printd("on_message_begin\n");
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_MESSAGE_BEGIN;
    return 0;
}

int on_headers_complete(http_parser* parser) {
    printd("on_headers_complete\n");
    Http1Session* hss = (Http1Session*)parser->data;
    hss->handle_header();
    auto iter = hss->parsed->headers.find("content-type");
    if (iter != hss->parsed->headers.end()) {
        hss->parsed->content_type = http_content_type_enum(iter->second.c_str());
    }
    hss->parsed->http_major = parser->http_major;
    hss->parsed->http_minor = parser->http_minor;
    if (hss->parsed->type == HTTP_REQUEST) {
        HttpRequest* req = (HttpRequest*)hss->parsed;
        req->method = (http_method)parser->method;
        req->url = hss->url;
    }
    else if (hss->parsed->type == HTTP_RESPONSE) {
        HttpResponse* res = (HttpResponse*)hss->parsed;
        res->status_code = (http_status)parser->status_code;
    }
    hss->state = HP_HEADERS_COMPLETE;
    return 0;
}

int on_message_complete(http_parser* parser) {
    printd("on_message_complete\n");
    Http1Session* hss = (Http1Session*)parser->data;
    hss->state = HP_MESSAGE_COMPLETE;
    return 0;
}

#ifdef WITH_NGHTTP2
#include "nghttp2/nghttp2.h"
#include "http2def.h"
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
    nv.namelen = namelen;
    nv.valuelen = valuelen;
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

enum http2_session_state {
    HSS_SEND_MAGIC,
    HSS_SEND_SETTINGS,
    HSS_SEND_HEADERS,
    HSS_SEND_DATA_FRAME_HD,
    HSS_SEND_DATA
};

class Http2Session : public HttpSession {
public:
    static nghttp2_session_callbacks* cbs;
    nghttp2_session*                session;
    http2_session_state             state;
    HttpPayload*                    submited;
    HttpPayload*                    parsed;
    int error;
    int stream_id;
    int stream_closed;
    unsigned char                   frame_hdbuf[HTTP2_FRAME_HDLEN];

    Http2Session(http_session_type type) {
        if (cbs == NULL) {
            nghttp2_session_callbacks_new(&cbs);
            nghttp2_session_callbacks_set_on_header_callback(cbs, on_header_callback);
            nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, on_data_chunk_recv_callback);
            nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, on_frame_recv_callback);
        }
        if (type == HTTP_CLIENT) {
            nghttp2_session_client_new(&session, cbs, NULL);
            state = HSS_SEND_MAGIC;
        }
        else if (type == HTTP_SERVER) {
            nghttp2_session_server_new(&session, cbs, NULL);
            state = HSS_SEND_SETTINGS;
        }
        nghttp2_session_set_user_data(session, this);
        submited = NULL;
        parsed = NULL;
        stream_id = -1;
        stream_closed = 0;

        nghttp2_settings_entry settings[] = {
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}
        };
        nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings, ARRAY_SIZE(settings));
        state = HSS_SEND_SETTINGS;
    }

    virtual ~Http2Session() {
        if (session) {
            nghttp2_session_del(session);
            session = NULL;
        }
    }

    virtual int GetSendData(char** data, size_t* len) {
        // HTTP2_MAGIC,HTTP2_SETTINGS,HTTP2_HEADERS
        *len = nghttp2_session_mem_send(session, (const uint8_t**)data);
        if (*len == 0) {
            if (submited) {
                void* content = submited->Content();
                int content_length = submited->ContentLength();
                if (content_length != 0) {
                    if (state == HSS_SEND_HEADERS) {
                        // HTTP2_DATA_FRAME_HD
                        state = HSS_SEND_DATA_FRAME_HD;
                        http2_frame_hd hd;
                        hd.length = content_length;
                        hd.type = HTTP2_DATA;
                        hd.flags = HTTP2_FLAG_END_STREAM;
                        hd.stream_id = stream_id;
                        http2_frame_hd_pack(&hd, frame_hdbuf);
                        *data = (char*)frame_hdbuf;
                        *len = HTTP2_FRAME_HDLEN;
                    }
                    else if (state == HSS_SEND_DATA_FRAME_HD) {
                        // HTTP2_DATA
                        state = HSS_SEND_DATA;
                        *data = (char*)content;
                        *len = content_length;
                    }
                }
            }
        }
        return *len;
    }

    virtual int FeedRecvData(const char* data, size_t len) {
        int ret = nghttp2_session_mem_recv(session, (const uint8_t*)data, len);
        if (ret != len) {
            error = ret;
        }
        return ret;
    }

    virtual bool WantRecv() {
        return stream_id == -1 || stream_closed == 0;
    }

    virtual int SubmitRequest(HttpRequest* req) {
        submited = req;

        std::vector<nghttp2_nv> nvs;
        char c_str[256] = {0};
        req->ParseUrl();
        nvs.push_back(make_nv(":method", http_method_str(req->method)));
        nvs.push_back(make_nv(":path", req->path.c_str()));
        nvs.push_back(make_nv(":scheme", req->https ? "https" : "http"));
        if (req->port == 0 ||
            req->port == DEFAULT_HTTP_PORT ||
            req->port == DEFAULT_HTTPS_PORT) {
            nvs.push_back(make_nv(":authority", req->host.c_str()));
        }
        else {
            snprintf(c_str, sizeof(c_str), "%s:%d", req->host.c_str(), req->port);
            nvs.push_back(make_nv(":authority", c_str));
        }
        req->FillContentType();
        req->FillContentLength();
        const char* name;
        const char* value;
        for (auto& header : req->headers) {
            name = header.first.c_str();
            value = header.second.c_str();
            strlower((char*)name);
            if (strcmp(name, "connection") == 0) {
                // HTTP2 use stream
                continue;
            }
            nvs.push_back(make_nv2(name, value, header.first.size(), header.second.size()));
        }
        int flags = NGHTTP2_FLAG_END_HEADERS;
        if (req->ContentLength() == 0) {
            flags |= NGHTTP2_FLAG_END_STREAM;
        }
        stream_id = nghttp2_submit_headers(session, flags, -1, NULL, &nvs[0], nvs.size(), NULL);
        // avoid DATA_SOURCE_COPY, we do not use nghttp2_submit_data
        // nghttp2_data_provider data_prd;
        // data_prd.read_callback = data_source_read_callback;
        //stream_id = nghttp2_submit_request(session, NULL, &nvs[0], nvs.size(), &data_prd, NULL);
        stream_closed = 0;
        state = HSS_SEND_HEADERS;
        return 0;
    }

    virtual int SubmitResponse(HttpResponse* res) {
        submited = res;

        std::vector<nghttp2_nv> nvs;
        char c_str[256] = {0};
        snprintf(c_str, sizeof(c_str), "%d", res->status_code);
        nvs.push_back(make_nv(":status", c_str));
        res->FillContentType();
        res->FillContentLength();

        const char* name;
        const char* value;
        for (auto& header : res->headers) {
            name = header.first.c_str();
            value = header.second.c_str();
            strlower((char*)name);
            if (strcmp(name, "connection") == 0) {
                // HTTP2 use stream
                continue;
            }
            nvs.push_back(make_nv2(name, value, header.first.size(), header.second.size()));
        }
        int flags = NGHTTP2_FLAG_END_HEADERS;
        if (res->ContentLength() == 0) {
            flags |= NGHTTP2_FLAG_END_STREAM;
        }
        nghttp2_submit_headers(session, flags, stream_id, NULL, &nvs[0], nvs.size(), NULL);
        // avoid DATA_SOURCE_COPY, we do not use nghttp2_submit_data
        // data_prd.read_callback = data_source_read_callback;
        //stream_id = nghttp2_submit_request(session, NULL, &nvs[0], nvs.size(), &data_prd, NULL);
        //nghttp2_submit_response(session, stream_id, &nvs[0], nvs.size(), &data_prd);
        stream_closed = 0;
        state = HSS_SEND_HEADERS;
        return 0;
    }

    virtual int InitResponse(HttpResponse* res) {
        res->Reset();
        res->http_major = 2;
        res->http_minor = 0;
        parsed = res;
        return 0;
    }

    virtual int InitRequest(HttpRequest* req) {
        req->Reset();
        req->http_major = 2;
        req->http_minor = 0;
        parsed = req;
        return 0;
    }

    virtual int GetError() {
        return error;
    }

    virtual const char* StrError(int error) {
        return nghttp2_http2_strerror(error);
    }
};

nghttp2_session_callbacks* Http2Session::cbs = NULL;

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
    Http2Session* hss = (Http2Session*)userdata;
    if (*name == ':') {
        if (hss->parsed->type == HTTP_REQUEST) {
            // :method :path :scheme :authority
            HttpRequest* req = (HttpRequest*)hss->parsed;
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
        else if (hss->parsed->type == HTTP_RESPONSE) {
            HttpResponse* res = (HttpResponse*)hss->parsed;
            if (strcmp(name, ":status") == 0) {
                res->status_code = (http_status)atoi(value);
            }
        }
    }
    else {
        hss->parsed->headers[name] = value;
        if (strcmp(name, "content-type") == 0) {
            hss->parsed->content_type = http_content_type_enum(value);
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
    Http2Session* hss = (Http2Session*)userdata;
    hss->parsed->body.insert(hss->parsed->body.size(), (const char*)data, len);
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session,
        const nghttp2_frame *frame, void *userdata) {
    printd("on_frame_recv_callback\n");
    print_frame_hd(&frame->hd);
    Http2Session* hss = (Http2Session*)userdata;
    switch (frame->hd.type) {
    case NGHTTP2_DATA:
    case NGHTTP2_HEADERS:
        hss->stream_id = frame->hd.stream_id;
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            printd("on_stream_closed stream_id=%d\n", hss->stream_id);
            hss->stream_closed = 1;
        }
        break;
    default:
        break;
    }

    return 0;
}
#endif

HttpSession* HttpSession::New(http_session_type type, http_version version) {
    if (version == HTTP_V1) {
        return new Http1Session;
    }
    else if (version == HTTP_V2) {
#ifdef WITH_NGHTTP2
        return new Http2Session(type);
#else
        fprintf(stderr, "Please recompile WITH_NGHTTP2!\n");
#endif
    }

    return NULL;
}
