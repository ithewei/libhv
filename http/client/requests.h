#ifndef HV_REQUESTS_H_
#define HV_REQUESTS_H_

/*
 * Inspired by python requests
 *
 * @code

#include "requests.h"

int main() {
    auto resp = requests::get("http://127.0.0.1:8080/ping");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    resp = requests::post("http://127.0.0.1:8080/echo", "hello,world!");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    return 0;
}

**/

#include <memory>
#include "HttpClient.h"

namespace requests {

typedef HttpRequestPtr          Request;
typedef HttpResponsePtr         Response;
typedef HttpResponseCallback    ResponseCallback;

HV_INLINE Response request(Request req) {
    auto resp = std::make_shared<HttpResponse>();
    int ret = http_client_send(req.get(), resp.get());
    return ret ? NULL : resp;
}

HV_INLINE Response request(http_method method, const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    if (&body != &NoBody) {
        req->body = body;
    }
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }
    return request(req);
}

HV_INLINE Response head(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_HEAD, url, NoBody, headers);
}

HV_INLINE Response get(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_GET, url, NoBody, headers);
}

HV_INLINE Response post(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_POST, url, body, headers);
}

HV_INLINE Response put(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PUT, url, body, headers);
}

HV_INLINE Response patch(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_PATCH, url, body, headers);
}

// delete is c++ keyword, we have to replace delete with Delete.
HV_INLINE Response Delete(const char* url, const http_headers& headers = DefaultHeaders) {
    return request(HTTP_DELETE, url, NoBody, headers);
}

HV_INLINE int async(Request req, ResponseCallback resp_cb) {
    return http_client_send_async(req, std::move(resp_cb));
}

// Sample codes for uploading and downloading files
HV_INLINE Response uploadFile(const char* url, const char* filepath, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders) {
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    req->timeout = 600; // 10min
    if (req->File(filepath) != 200) return NULL;
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }
    return request(req);
}

#ifndef WITHOUT_HTTP_CONTENT
HV_INLINE Response uploadFormFile(const char* url, const char* name, const char* filepath, std::map<std::string, std::string>& params = hv::empty_map, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders) {
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    req->timeout = 600; // 10min
    req->content_type = MULTIPART_FORM_DATA;
    req->SetFormFile(name, filepath);
    for (auto& param : params) {
        req->SetFormData(param.first.c_str(), param.second);
    }
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }
    return request(req);
}
#endif

typedef std::function<void(size_t sended_bytes, size_t total_bytes)>    upload_progress_cb;

// Internal helper for streaming file data through an established connection
// Used by uploadLargeFile() and uploadLargeFormFile()
namespace internal {
HV_INLINE int streamFileToConnection(
    hv::HttpClient& cli,
    HFile& file,
    size_t file_size,
    upload_progress_cb progress_cb = NULL)
{
    size_t sent_bytes = 0;
    char buf[40960]; // 40K
    while (sent_bytes < file_size) {
        size_t to_read = file_size - sent_bytes;
        if (to_read > sizeof(buf)) to_read = sizeof(buf);
        int nread = file.read(buf, to_read);
        if (nread <= 0) return -1;
        int nsend = cli.sendData(buf, nread);
        if (nsend != nread) return -1;
        sent_bytes += nsend;
        if (progress_cb) progress_cb(sent_bytes, file_size);
    }
    return 0;
}
} // namespace internal

HV_INLINE Response uploadLargeFile(const char* url, const char* filepath, upload_progress_cb progress_cb = NULL, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders) {
    // open file
    HFile file;
    int ret = file.open(filepath, "rb");
    if (ret != 0) {
        return NULL;
    }

    hv::HttpClient cli;
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    req->timeout = 3600; // 1h
    if (&headers != &DefaultHeaders) {
        req->headers = headers;
    }

    // connect
    req->ParseUrl();
    int connfd = cli.connect(req->host.c_str(), req->port, req->IsHttps(), req->connect_timeout);
    if (connfd < 0) {
        return NULL;
    }

    // send header
    size_t total_bytes = file.size(filepath);
    req->SetHeader("Content-Length", hv::to_string(total_bytes));
    ret = cli.sendHeader(req.get());
    if (ret != 0) {
        return NULL;
    }

    // stream file using shared helper
    if (internal::streamFileToConnection(cli, file, total_bytes, progress_cb) != 0) {
        return NULL;
    }

    // recv response
    auto resp = std::make_shared<HttpResponse>();
    ret = cli.recvResponse(resp.get());
    if (ret != 0) {
        return NULL;
    }
    return resp;
}

// Streaming multipart form file upload - memory efficient for large files
// Overload with explicit upload_filename for when the filename differs from local path
#ifndef WITHOUT_HTTP_CONTENT
HV_INLINE Response uploadLargeFormFile(const char* url, const char* name, const char* filepath,
    const char* upload_filename,
    std::map<std::string, std::string>& params = hv::empty_map,
    upload_progress_cb progress_cb = NULL, http_method method = HTTP_POST,
    const http_headers& headers = DefaultHeaders)
{
    HFile file;
    if (file.open(filepath, "rb") != 0) return NULL;

    size_t file_size = file.size(filepath);
    const char* filename = upload_filename ? upload_filename : hv_basename(filepath);
    static const char* BOUNDARY = "----libhvFormBoundary7MA4YWxkTrZu0gW";

    // Build preamble (form fields + file header)
    std::string preamble;
    for (auto& param : params) {
        preamble += "--"; preamble += BOUNDARY; preamble += "\r\n";
        preamble += "Content-Disposition: form-data; name=\"";
        preamble += param.first; preamble += "\"\r\n\r\n";
        preamble += param.second; preamble += "\r\n";
    }
    preamble += "--"; preamble += BOUNDARY; preamble += "\r\n";
    preamble += "Content-Disposition: form-data; name=\"";
    preamble += name; preamble += "\"; filename=\"";
    preamble += filename; preamble += "\"\r\n";
    preamble += "Content-Type: application/octet-stream\r\n\r\n";

    std::string epilogue = "\r\n--";
    epilogue += BOUNDARY; epilogue += "--\r\n";

    // Total = preamble + file + epilogue
    size_t total_length = preamble.size() + file_size + epilogue.size();

    hv::HttpClient cli;
    auto req = std::make_shared<HttpRequest>();
    req->method = method;
    req->url = url;
    req->timeout = 3600;
    req->SetHeader("Content-Type", std::string("multipart/form-data; boundary=") + BOUNDARY);
    req->SetHeader("Content-Length", hv::to_string(total_length));
    if (&headers != &DefaultHeaders) {
        for (auto& h : headers) req->SetHeader(h.first.c_str(), h.second);
    }

    req->ParseUrl();
    if (cli.connect(req->host.c_str(), req->port, req->IsHttps(), req->connect_timeout) < 0) {
        return NULL;
    }
    if (cli.sendHeader(req.get()) != 0) return NULL;

    // Send preamble
    if (cli.sendData(preamble.data(), preamble.size()) != (int)preamble.size()) return NULL;

    // Stream file using shared helper
    if (internal::streamFileToConnection(cli, file, file_size, progress_cb) != 0) {
        return NULL;
    }

    // Send epilogue
    if (cli.sendData(epilogue.data(), epilogue.size()) != (int)epilogue.size()) return NULL;

    auto resp = std::make_shared<HttpResponse>();
    if (cli.recvResponse(resp.get()) != 0) return NULL;
    return resp;
}

// Convenience overload - uses basename of filepath as upload filename
HV_INLINE Response uploadLargeFormFile(const char* url, const char* name, const char* filepath,
    std::map<std::string, std::string>& params = hv::empty_map,
    upload_progress_cb progress_cb = NULL, http_method method = HTTP_POST,
    const http_headers& headers = DefaultHeaders)
{
    return uploadLargeFormFile(url, name, filepath, NULL, params, progress_cb, method, headers);
}
#endif

// see examples/wget.cpp
typedef std::function<void(size_t received_bytes, size_t total_bytes)> download_progress_cb;
HV_INLINE size_t downloadFile(const char* url, const char* filepath, download_progress_cb progress_cb = NULL) {
    // open file
    std::string filepath_download(filepath);
    filepath_download += ".download";
    HFile file;
    int ret = file.open(filepath_download.c_str(), "wb");
    if (ret != 0) {
        return 0;
    }
    // download
    auto req = std::make_shared<HttpRequest>();
    req->method = HTTP_GET;
    req->url = url;
    req->timeout = 3600; // 1h
    size_t content_length = 0;
    size_t received_bytes = 0;
    req->http_cb = [&file, &content_length, &received_bytes, &progress_cb]
        (HttpMessage* resp, http_parser_state state, const char* data, size_t size) {
        if (!resp->headers["Location"].empty()) return;
        if (state == HP_HEADERS_COMPLETE) {
            content_length = hv::from_string<size_t>(resp->GetHeader("Content-Length"));
        } else if (state == HP_BODY) {
            if (data && size) {
                // write file
                file.write(data, size);
                received_bytes += size;
                if (progress_cb) {
                    progress_cb(received_bytes, content_length);
                }
            }
        }
    };
    auto resp = request(req);
    file.close();
    if (resp == NULL || resp->status_code != 200) {
        return 0;
    }
    // check filesize
    if (content_length != 0 && hv_filesize(filepath_download.c_str()) != content_length) {
        remove(filepath_download.c_str());
        return 0;
    }
    rename(filepath_download.c_str(), filepath);
    return hv_filesize(filepath);
}

}

#endif // HV_REQUESTS_H_
