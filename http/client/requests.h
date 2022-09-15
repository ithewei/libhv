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
#include "http_client.h"

namespace requests {

typedef HttpRequestPtr          Request;
typedef HttpResponsePtr         Response;
typedef HttpResponseCallback    ResponseCallback;

HV_INLINE Response request(Request req) {
    Response resp(new HttpResponse);
    int ret = http_client_send(req.get(), resp.get());
    return ret ? NULL : resp;
}

HV_INLINE Response request(http_method method, const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders) {
    Request req(new HttpRequest);
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

HV_INLINE Response uploadFile(const char* url, const char* filepath, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders) {
    Request req(new HttpRequest);
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
    Request req(new HttpRequest);
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
    Request req(new HttpRequest);
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
    if (content_length != 0) {
        if (hv_filesize(filepath_download.c_str()) == content_length) {
            rename(filepath_download.c_str(), filepath);
        } else {
            remove(filepath_download.c_str());
            return 0;
        }
    }
    return hv_filesize(filepath);
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

}

#endif // HV_REQUESTS_H_
