#ifndef HV_HTTP_RESPONSE_WRITER_H_
#define HV_HTTP_RESPONSE_WRITER_H_

#include "Channel.h"
#include "HttpMessage.h"

namespace hv {

class HttpResponseWriter : public SocketChannel {
public:
    HttpResponsePtr response;
    enum State {
        SEND_BEGIN = 0,
        SEND_HEADER,
        SEND_BODY,
        SEND_CHUNKED,
        SEND_CHUNKED_END,
        SEND_END,
    } state: 8, end: 8;
    HttpResponseWriter(hio_t* io, const HttpResponsePtr& resp)
        : SocketChannel(io)
        , response(resp)
        , state(SEND_BEGIN)
        , end(SEND_BEGIN)
    {}
    ~HttpResponseWriter() {}

    // Begin -> End
    // Begin -> WriteResponse -> End
    // Begin -> WriteStatus -> WriteHeader -> WriteBody -> End
    // Begin -> EndHeaders("Content-Type", "text/event-stream") -> write -> write -> ... -> close
    // Begin -> EndHeaders("Content-Length", content_length) -> WriteBody -> WriteBody -> ... -> End
    // Begin -> EndHeaders("Transfer-Encoding", "chunked") -> WriteChunked -> WriteChunked -> ... -> End

    int Begin() {
        state = end = SEND_BEGIN;
        return 0;
    }

    int WriteStatus(http_status status_codes) {
        response->status_code = status_codes;
        return 0;
    }

    int WriteHeader(const char* key, const char* value) {
        response->SetHeader(key, value);
        return 0;
    }

    template<typename T>
    int WriteHeader(const char* key, T num) {
        response->SetHeader(key, hv::to_string(num));
        return 0;
    }

    int WriteCookie(const HttpCookie& cookie) {
        response->cookies.push_back(cookie);
        return 0;
    }

    int EndHeaders(const char* key = NULL, const char* value = NULL) {
        if (state != SEND_BEGIN) return -1;
        if (key && value) {
            response->SetHeader(key, value);
        }
        std::string headers = response->Dump(true, false);
        state = SEND_HEADER;
        return write(headers);
    }

    template<typename T>
    int EndHeaders(const char* key, T num) {
        std::string value = hv::to_string(num);
        return EndHeaders(key, value.c_str());
    }

    int WriteChunked(const char* buf, int len = -1) {
        int ret = 0;
        if (len == -1) len = strlen(buf);
        if (state == SEND_BEGIN) {
            EndHeaders("Transfer-Encoding", "chunked");
        }
        char chunked_header[64];
        int chunked_header_len = snprintf(chunked_header, sizeof(chunked_header), "%x\r\n", len);
        write(chunked_header, chunked_header_len);
        if (buf && len) {
            state = SEND_CHUNKED;
            ret = write(buf, len);
        } else {
            state = SEND_CHUNKED_END;
        }
        write("\r\n", 2);
        return ret;
    }

    int WriteChunked(const std::string& str) {
        return WriteChunked(str.c_str(), str.size());
    }

    int EndChunked() {
        return WriteChunked(NULL, 0);
    }

    int WriteBody(const char* buf, int len = -1) {
        if (response->IsChunked()) {
            return WriteChunked(buf, len);
        }

        if (len == -1) len = strlen(buf);
        if (state == SEND_BEGIN) {
            response->body.append(buf, len);
            return len;
        } else {
            state = SEND_BODY;
            return write(buf, len);
        }
    }

    int WriteBody(const std::string& str) {
        return WriteBody(str.c_str(), str.size());
    }

    int WriteResponse(HttpResponse* resp) {
        if (resp == NULL) {
            response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
            return 0;
        }
        bool is_dump_headers = state == SEND_BEGIN ? true : false;
        std::string msg = resp->Dump(is_dump_headers, true);
        state = SEND_BODY;
        return write(msg);
    }

    int SSEvent(const std::string& data, const char* event = "message") {
        if (state == SEND_BEGIN) {
            EndHeaders("Content-Type", "text/event-stream");
        }
        std::string msg;
        msg =  "event: "; msg += event; msg += "\n";
        msg += "data: ";  msg += data;  msg += "\n\n";
        state = SEND_BODY;
        return write(msg);
    }

    int End(const char* buf = NULL, int len = -1) {
        if (end == SEND_END) return 0;
        end = SEND_END;

        if (!isConnected()) {
            return -1;
        }

        int ret = 0;
        bool keepAlive = response->IsKeepAlive();
        if (state == SEND_CHUNKED) {
            if (buf) {
                ret = WriteChunked(buf, len);
            }
            if (state == SEND_CHUNKED) {
                EndChunked();
            }
        } else {
            if (buf) {
                ret = WriteBody(buf, len);
            }
            bool is_dump_headers = true;
            bool is_dump_body = true;
            if (state == SEND_HEADER) {
                is_dump_headers = false;
            } else if (state == SEND_BODY) {
                is_dump_headers = false;
                is_dump_body = false;
            }
            if (is_dump_body) {
                std::string msg = response->Dump(is_dump_headers, is_dump_body);
                state = SEND_BODY;
                ret = write(msg);
            }
        }

        if (!keepAlive) {
            close(true);
        }
        return ret;
    }

    int End(const std::string& str) {
        return End(str.c_str(), str.size());
    }
};

}

typedef std::shared_ptr<hv::HttpResponseWriter> HttpResponseWriterPtr;

#endif // HV_HTTP_RESPONSE_WRITER_H_
