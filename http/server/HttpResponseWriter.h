#ifndef HV_HTTP_RESPONSE_WRITER_H_
#define HV_HTTP_RESPONSE_WRITER_H_

#include "Channel.h"
#include "HttpMessage.h"

namespace hv {

class HttpResponseWriter : public SocketChannel {
public:
    HttpResponsePtr response;
    enum State {
        SEND_BEGIN,
        SEND_HEADER,
        SEND_BODY,
        SEND_END,
    } state;
    HttpResponseWriter(hio_t* io, const HttpResponsePtr& resp)
        : SocketChannel(io)
        , response(resp)
        , state(SEND_BEGIN)
    {}
    ~HttpResponseWriter() {}

    // Begin -> End
    // Begin -> WriteResponse -> End
    // Begin -> WriteStatus -> WriteHeader -> WriteBody -> End
    // Begin -> WriteHeader -> EndHeaders -> WriteBody -> WriteBody -> ... -> End

    int Begin() {
        state = SEND_BEGIN;
        return 0;
    }

    int WriteStatus(http_status status_codes) {
        response->status_code = status_codes;
        return 0;
    }

    int WriteHeader(const char* key, const char* value) {
        response->headers[key] = value;
        return 0;
    }

    int EndHeaders(const char* key = NULL, const char* value = NULL) {
        if (state != SEND_BEGIN) return -1;
        if (key && value) {
            response->headers[key] = value;
        }
        std::string headers = response->Dump(true, false);
        state = SEND_HEADER;
        return write(headers);
    }

    int WriteBody(const char* buf, int len = -1) {
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

    int End(const char* buf = NULL, int len = -1) {
        if (state == SEND_END) return 0;
        int ret = 0;
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
            ret = write(msg);
        }
        state = SEND_END;
        if (!response->IsKeepAlive()) {
            close();
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
