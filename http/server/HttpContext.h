#ifndef HV_HTTP_CONTEXT_H_
#define HV_HTTP_CONTEXT_H_

#include "hexport.h"
#include "HttpMessage.h"
#include "HttpResponseWriter.h"

namespace hv {

struct HttpService;
struct HV_EXPORT HttpContext {
    HttpService*            service;
    HttpRequestPtr          request;
    HttpResponsePtr         response;
    HttpResponseWriterPtr   writer;
    void*                   userdata;

    HttpContext() {
        service = NULL;
        userdata = NULL;
    }

    // HttpRequest aliases
    // return request->xxx
    std::string ip() {
        return request->client_addr.ip;
    }

    int port() {
        return request->client_addr.port;
    }

    http_method method() {
        return request->method;
    }

    std::string url() {
        return request->url;
    }

    std::string path() {
        return request->Path();
    }

    std::string fullpath() {
        return request->FullPath();
    }

    std::string host() {
        return request->Host();
    }

    const http_headers& headers() {
        return request->headers;
    }

    std::string header(const char* key, const std::string& defvalue = hv::empty_string) {
        return request->GetHeader(key, defvalue);
    }

    const hv::QueryParams& params() {
        return request->query_params;
    }

    std::string param(const char* key, const std::string& defvalue = hv::empty_string) {
        return request->GetParam(key, defvalue);
    }

    const HttpCookie& cookie(const char* name) {
        return request->GetCookie(name);
    }

    int length() {
        return request->ContentLength();
    }

    http_content_type type() {
        return request->ContentType();
    }

    bool is(http_content_type content_type) {
        return request->ContentType() == content_type;
    }

    bool is(const char* content_type) {
        return request->ContentType() == http_content_type_enum(content_type);
    }

    std::string& body() {
        return request->body;
    }

#ifndef WITHOUT_HTTP_CONTENT
    // Content-Type: application/json
    const hv::Json& json() {
        return request->GetJson();
    }

    // Content-Type: multipart/form-data
    const hv::MultiPart& form() {
        return request->GetForm();
    }
    std::string form(const char* name, const std::string& defvalue = hv::empty_string) {
        return request->GetFormData(name, defvalue);
    }

    // Content-Type: application/x-www-form-urlencoded
    const hv::KeyValue& urlencoded() {
        return request->GetUrlEncoded();
    }
    std::string urlencoded(const char* key, const std::string& defvalue = hv::empty_string) {
        return request->GetUrlEncoded(key, defvalue);
    }

    // T=[bool, int, int64_t, float, double]
    template<typename T>
    T get(const char* key, T defvalue = 0) {
        return request->Get(key, defvalue);
    }
    std::string get(const char* key, const std::string& defvalue = hv::empty_string) {
        return request->GetString(key, defvalue);
    }
#endif

    // HttpResponse aliases
    // response->xxx = xxx
    void setStatus(http_status status) {
        response->status_code = status;
    }

    void setContentType(http_content_type type) {
        response->content_type = type;
    }

    void setContentType(const char* type) {
        response->content_type = http_content_type_enum(type);
    }

    void setHeader(const char* key, const std::string& value) {
        response->headers[key] = value;
        if (stricmp(key, "Content-Type") == 0) {
            setContentType(value.c_str());
        }
    }

    void setCookie(const HttpCookie& cookie) {
        response->AddCookie(cookie);
    }

    void setBody(const std::string& body) {
        response->body = body;
    }

    // response->sendXxx
    int send() {
        if (writer) {
            writer->End();
        }
        return response->status_code;
    }

    int send(const std::string& str, http_content_type type = APPLICATION_JSON) {
        response->content_type = type;
        response->body = str;
        return send();
    }

    int sendString(const std::string& str) {
        response->String(str);
        return send();
    }

    int sendData(void* data, int len, bool nocopy = true) {
        response->Data(data, len, nocopy);
        return send();
    }

    int sendFile(const char* filepath) {
        response->File(filepath);
        return send();
    }

#ifndef WITHOUT_HTTP_CONTENT
    // T=[bool, int, int64_t, float, double, string]
    template<typename T>
    void set(const char* key, const T& value) {
        response->Set(key, value);
    }

    // @see     HttpMessage::Json
    // @usage   https://github.com/nlohmann/json
    template<typename T>
    int sendJson(const T& t) {
        response->Json(t);
        return send();
    }
#endif

    int redirect(const std::string& location, http_status status = HTTP_STATUS_FOUND) {
        response->Redirect(location, status);
        return send();
    }

    int close() {
        return writer ? writer->close(true) : -1;
    }
};

} // end namespace hv

typedef std::shared_ptr<hv::HttpContext> HttpContextPtr;

#endif // HV_HTTP_CONTEXT_H_
