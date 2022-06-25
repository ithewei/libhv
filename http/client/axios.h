#ifndef HV_AXIOS_H_

#include "json.hpp"
#include "requests.h"

/*
 * Inspired by js axios
 *
 * @code

#include "axios.h"

int main() {
    const char* strReq = R"(
    {
        "method": "POST",
        "url": "http://127.0.0.1:8080/echo",
        "timeout": 10,
        "params": {
            "page_no": "1",
            "page_size": "10"
        },
        "headers": {
            "Content-Type": "application/json"
        },
        "body": {
            "app_id": "123456",
            "app_secret": "abcdefg"
        }
    }
    )";

    // sync
    auto resp = axios::axios(strReq);
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%s\n", resp->body.c_str());
    }

    // async
    int finished = 0;
    axios::axios(strReq, [&finished](const HttpResponsePtr& resp) {
        if (resp == NULL) {
            printf("request failed!\n");
        } else {
            printf("%s\n", resp->body.c_str());
        }
        finished = 1;
    });

    // wait async finished
    while (!finished) hv_sleep(1);
    return 0;
}

**/

using nlohmann::json;
using requests::Request;
using requests::Response;
using requests::ResponseCallback;

namespace axios {

HV_INLINE Request newRequestFromJson(const json& jreq) {
    Request req(new HttpRequest);
    // url
    if (jreq.contains("url")) {
        req->url = jreq["url"];
    }
    // params
    if (jreq.contains("params")) {
        req->query_params = jreq["params"].get<hv::QueryParams>();
    }
    // headers
    if (jreq.contains("headers")) {
        req->headers = jreq["headers"].get<http_headers>();
    }
    // body/data
    const char* body_field = nullptr;
    if (jreq.contains("body")) {
        body_field = "body";
    } else if (jreq.contains("data")) {
        body_field = "data";
    }
    if (body_field) {
        const json& jbody = jreq[body_field];
        if (jbody.is_object() || jbody.is_array()) {
            req->json = jbody;
        } else if (jbody.is_string()) {
            req->body = jbody;
        }
    }
    // method
    if (jreq.contains("method")) {
        std::string method = jreq["method"];
        req->method = http_method_enum(method.c_str());
    } else if (body_field) {
        req->method = HTTP_POST;
    } else {
        req->method = HTTP_GET;
    }
    // timeout
    if (jreq.contains("timeout")) {
        req->timeout = jreq["timeout"];
    }
    return req;
}

HV_INLINE Request newRequestFromJsonString(const char* req_str) {
    return newRequestFromJson(json::parse(req_str));
}

// sync
HV_INLINE Response axios(const json& jreq, http_method method = HTTP_GET, const char* url = nullptr) {
    auto req = newRequestFromJson(jreq);
    if (method != HTTP_GET) {
        req->method = method;
    }
    if (url) {
        req->url = url;
    }
    return req ? requests::request(req) : nullptr;
}

HV_INLINE Response axios(const char* req_str, http_method method = HTTP_GET, const char* url = nullptr) {
    return req_str  ? axios(json::parse(req_str), method, url)
                    : requests::request(method, url);
}

HV_INLINE Response head(const char* url, const json& jreq) {
    return axios(jreq, HTTP_HEAD, url);
}

HV_INLINE Response head(const char* url, const char* req_str = nullptr) {
    return axios(req_str, HTTP_HEAD, url);
}

HV_INLINE Response get(const char* url, const json& jreq) {
    return axios(jreq, HTTP_GET, url);
}

HV_INLINE Response get(const char* url, const char* req_str = nullptr) {
    return axios(req_str, HTTP_GET, url);
}

HV_INLINE Response post(const char* url, const json& jreq) {
    return axios(jreq, HTTP_POST, url);
}

HV_INLINE Response post(const char* url, const char* req_str = nullptr) {
    return axios(req_str, HTTP_POST, url);
}

HV_INLINE Response put(const char* url, const json& jreq) {
    return axios(jreq, HTTP_PUT, url);
}

HV_INLINE Response put(const char* url, const char* req_str = nullptr) {
    return axios(req_str, HTTP_PUT, url);
}

HV_INLINE Response patch(const char* url, const json& jreq) {
    return axios(jreq, HTTP_PATCH, url);
}

HV_INLINE Response patch(const char* url, const char* req_str = nullptr) {
    return axios(req_str, HTTP_PATCH, url);
}

HV_INLINE Response Delete(const char* url, const json& jreq) {
    return axios(jreq, HTTP_DELETE, url);
}

HV_INLINE Response Delete(const char* url, const char* req_str = nullptr) {
    return axios(req_str, HTTP_DELETE, url);
}

// async
HV_INLINE int axios(const json& jreq, ResponseCallback resp_cb) {
    auto req = newRequestFromJson(jreq);
    return req ? requests::async(req, std::move(resp_cb)) : -1;
}

HV_INLINE int axios(const char* req_str, ResponseCallback resp_cb) {
    return axios(json::parse(req_str), std::move(resp_cb));
}

}

#endif // HV_AXIOS_H_
