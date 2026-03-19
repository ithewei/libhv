#include "HttpResponseWriter.h"

#include "HttpService.h"
#include "http_compress.h"
#include "hlog.h"

namespace hv {

int HttpResponseWriter::EndHeaders(const char* key /* = NULL */, const char* value /* = NULL */) {
    if (state == SEND_END || end == SEND_END) return 0;
    if (state != SEND_BEGIN) return -1;
    if (key && value) {
        response->SetHeader(key, value);
    }
    if (request && service) {
        HttpCompressionOptions options = response->compression_inherit ? service->compression : response->compression;
        http_content_encoding encoding = HTTP_CONTENT_ENCODING_IDENTITY;
        int ret = ApplyResponseCompression(request.get(), response.get(), options, true, &encoding);
        if (ret != 0) {
            hlogw("response compression failed: %d", ret);
            return ret;
        }
        if (response->status_code == HTTP_STATUS_NOT_ACCEPTABLE) {
            std::string msg = response->Dump(true, true);
            state = SEND_END;
            end = SEND_END;
            ret = write(msg);
            if (!response->IsKeepAlive()) {
                close(true);
            }
            return ret;
        }
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
    if (state == SEND_END || end == SEND_END) return 0;
    int ret = 0;
    if (len == -1) len = strlen(buf);
    if (state == SEND_BEGIN) {
        ret = EndHeaders("Transfer-Encoding", "chunked");
        if (state == SEND_END || end == SEND_END) return ret;
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
    if (state == SEND_END || end == SEND_END) return 0;
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
    if (state == SEND_END || end == SEND_END) return 0;
    if (resp == NULL) {
        response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return 0;
    }
    if (state == SEND_BEGIN && request && service) {
        HttpCompressionOptions options = resp->compression_inherit ? service->compression : resp->compression;
        int ret = ApplyResponseCompression(request.get(), resp, options, false);
        if (ret != 0) {
            hlogw("response compression failed: %d", ret);
            return ret;
        }
    }
    bool is_dump_headers = state == SEND_BEGIN ? true : false;
    std::string msg = resp->Dump(is_dump_headers, true);
    state = SEND_BODY;
    return write(msg);
}

int HttpResponseWriter::SSEvent(const std::string& data, const char* event /* = "message" */) {
    if (state == SEND_END || end == SEND_END) return 0;
    if (state == SEND_BEGIN) {
        int ret = EndHeaders("Content-Type", "text/event-stream");
        if (state == SEND_END || end == SEND_END) return ret;
    }
    std::string msg;
    if (event) {
        msg = "event: "; msg += event; msg += "\n";
    }
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
            if (state == SEND_BEGIN && request && service) {
                HttpCompressionOptions options = response->compression_inherit ? service->compression : response->compression;
                int apply_ret = ApplyResponseCompression(request.get(), response.get(), options, false);
                if (apply_ret != 0) {
                    hlogw("response compression failed: %d", apply_ret);
                    return apply_ret;
                }
            }
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
