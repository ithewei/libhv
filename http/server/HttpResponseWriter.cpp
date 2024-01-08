#include "HttpResponseWriter.h"

namespace hv {

int HttpResponseWriter::EndHeaders(const char* key /* = NULL */, const char* value /* = NULL */) {
    if (state != SEND_BEGIN) return -1;
    if (key && value) {
        response->SetHeader(key, value);
    }
    std::string headers = response->Dump(true, false);
    // erase Content-Length: 0\r\n
    std::string content_length_0("Content-Length: 0\r\n");
    auto pos = headers.find(content_length_0);
    if (pos != std::string::npos) {
        headers.erase(pos, content_length_0.size());
    }
    state = SEND_HEADER;
    return write(headers);
}

int HttpResponseWriter::WriteChunked(const char* buf, int len /* = -1 */) {
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

int HttpResponseWriter::WriteBody(const char* buf, int len /* = -1 */) {
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

int HttpResponseWriter::WriteResponse(HttpResponse* resp) {
    if (resp == NULL) {
        response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return 0;
    }
    bool is_dump_headers = state == SEND_BEGIN ? true : false;
    std::string msg = resp->Dump(is_dump_headers, true);
    state = SEND_BODY;
    return write(msg);
}

int HttpResponseWriter::SSEvent(const std::string& data, const char* event /* = "message" */) {
    if (state == SEND_BEGIN) {
        EndHeaders("Content-Type", "text/event-stream");
    }
    std::string msg;
    msg =  "event: "; msg += event; msg += "\n";
    msg += "data: ";  msg += data;  msg += "\n\n";
    state = SEND_BODY;
    return write(msg);
}

int HttpResponseWriter::End(const char* buf /* = NULL */, int len /* = -1 */) {
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

}
