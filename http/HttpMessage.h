#ifndef HTTP_MESSAGE_H_
#define HTTP_MESSAGE_H_

/*
 * @class HttpMessage
 * HttpRequest extends HttpMessage
 * HttpResponse extends HttpMessage
 *
 * @member
 * request-line: GET / HTTP/1.1\r\n => method path
 * response-line: 200 OK\r\n => status_code
 * headers
 * body
 *
 * content, content_length, content_type
 * json, form, kv
 *
 * @function
 * Content, ContentLength, ContentType
 * Get, Set
 * GetHeader, GetParam, GetString, GetBool, GetInt, GetFloat
 * String, Data, File, Json
 *
 * @example
 * see examples/httpd
 *
 */

#include <memory>
#include <string>
#include <map>
#include <functional>

#include "hexport.h"
#include "hbase.h"
#include "hstring.h"
#include "hfile.h"

#include "httpdef.h"
#include "http_content.h"

typedef std::map<std::string, std::string, StringCaseLess>  http_headers;
typedef std::string                                         http_body;

struct HNetAddr {
    std::string     ip;
    int             port;

    std::string ipport() {
        return asprintf("%s:%d", ip.c_str(), port);
    }
};

class HV_EXPORT HttpMessage {
public:
    int                 type;
    unsigned short      http_major;
    unsigned short      http_minor;

    http_headers        headers;
    http_body           body;

    // structured content
    void*               content;    // DATA_NO_COPY
    int                 content_length;
    http_content_type   content_type;
#ifndef WITHOUT_HTTP_CONTENT
    hv::Json            json;       // APPLICATION_JSON
    MultiPart           form;       // MULTIPART_FORM_DATA
    hv::KeyValue        kv;         // X_WWW_FORM_URLENCODED

    // T=[bool, int64_t, double]
    template<typename T>
    T Get(const char* key, T defvalue = 0);

    std::string GetString(const char* key, const std::string& = "");
    bool GetBool(const char* key, bool defvalue = 0);
    int64_t GetInt(const char* key, int64_t defvalue = 0);
    double GetFloat(const char* key, double defvalue = 0);

    template<typename T>
    void Set(const char* key, const T& value) {
        switch (content_type) {
        case APPLICATION_JSON:
            json[key] = value;
            break;
        case MULTIPART_FORM_DATA:
            form[key] = FormData(value);
            break;
        case X_WWW_FORM_URLENCODED:
            kv[key] = hv::to_string(value);
            break;
        default:
            break;
        }
    }

    /*
     * null:    Json(nullptr);
     * boolean: Json(true);
     * number:  Json(123);
     * string:  Json("hello");
     * object:  Json(std::map<string, ValueType>);
     *          Json(hv::Json::object({
                    {"k1", "v1"},
                    {"k2", "v2"}
                }));
     * array:   Json(std::vector<ValueType>);
                Json(hv::Json::object(
                    {1, 2, 3}
                ));
     */
    template<typename T>
    int Json(const T& t) {
        content_type = APPLICATION_JSON;
        json = t;
        return 200;
    }

    void UploadFormFile(const char* name, const char* filepath) {
        content_type = MULTIPART_FORM_DATA;
        form[name] = FormData(NULL, filepath);
    }

    int SaveFormFile(const char* name, const char* filepath) {
        if (content_type != MULTIPART_FORM_DATA) {
            return HTTP_STATUS_BAD_REQUEST;
        }
        FormData formdata = form[name];
        if (formdata.content.empty()) {
            return HTTP_STATUS_BAD_REQUEST;
        }
        HFile file;
        if (file.open(filepath, "wb") != 0) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        file.write(formdata.content.data(), formdata.content.size());
        return 200;
    }
#endif

    HttpMessage() {
        type = HTTP_BOTH;
        Init();
    }

    virtual ~HttpMessage() {}

    void Init() {
        http_major = 1;
        http_minor = 1;
        content = NULL;
        content_length = 0;
        content_type = CONTENT_TYPE_NONE;
    }

    virtual void Reset() {
        Init();
        headers.clear();
        body.clear();
#ifndef WITHOUT_HTTP_CONTENT
        json.clear();
        form.clear();
        kv.clear();
#endif
    }

    // structured-content -> content_type <-> headers Content-Type
    void FillContentType();
    // body.size -> content_length <-> headers Content-Length
    void FillContentLength();

    std::string GetHeader(const char* key, const std::string& defvalue = "") {
        auto iter = headers.find(key);
        if (iter != headers.end()) {
            return iter->second;
        }
        return defvalue;
    }

    // headers -> string
    void DumpHeaders(std::string& str);
    // structured content -> body
    void DumpBody();
    // body -> structured content
    // @retval 0:succeed
    int  ParseBody();

    virtual std::string Dump(bool is_dump_headers, bool is_dump_body);

    void* Content() {
        if (content == NULL && body.size() != 0) {
            content = (void*)body.data();
        }
        return content;
    }

    int ContentLength() {
        if (content_length == 0) {
            FillContentLength();
        }
        return content_length;
    }

    http_content_type ContentType() {
        if (content_type == CONTENT_TYPE_NONE) {
            FillContentType();
        }
        return content_type;
    }

    int String(const std::string& str) {
        content_type = TEXT_PLAIN;
        body = str;
        return 200;
    }

    int Data(void* data, int len) {
        content_type = APPLICATION_OCTET_STREAM;
        content = data;
        content_length = len;
        return 200;
    }

    int File(const char* filepath) {
        HFile file;
        if (file.open(filepath, "rb") != 0) {
            return HTTP_STATUS_NOT_FOUND;
        }
        const char* suffix = hv_suffixname(filepath);
        if (suffix) {
            content_type = http_content_type_enum_by_suffix(suffix);
        }
        if (content_type == CONTENT_TYPE_NONE || content_type == CONTENT_TYPE_UNDEFINED) {
            content_type = APPLICATION_OCTET_STREAM;
        }
        file.readall(body);
        return 200;
    }
};

#define DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36"
class HV_EXPORT HttpRequest : public HttpMessage {
public:
    http_method         method;
    // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
    std::string         url;
    // structured url
    bool                https;
    std::string         host;
    int                 port;
    std::string         path;
    QueryParams         query_params;
    // client_addr
    HNetAddr            client_addr; // for http server save client addr of request
    int                 timeout; // for http client timeout

    HttpRequest() : HttpMessage() {
        type = HTTP_REQUEST;
        Init();
    }

    void Init() {
        headers["User-Agent"] = DEFAULT_USER_AGENT;
        headers["Accept"] = "*/*";
        method = HTTP_GET;
        https = 0;
        host = "127.0.0.1";
        port = DEFAULT_HTTP_PORT;
        path = "/";
        timeout = 0;
    }

    virtual void Reset() {
        HttpMessage::Reset();
        Init();
        url.clear();
        query_params.clear();
    }

    virtual std::string Dump(bool is_dump_headers, bool is_dump_body);

    std::string GetParam(const char* key, const std::string& defvalue = "") {
        auto iter = query_params.find(key);
        if (iter != query_params.end()) {
            return iter->second;
        }
        return defvalue;
    }

    // structed url -> url
    void DumpUrl();
    // url -> structed url
    void ParseUrl();
};

class HV_EXPORT HttpResponse : public HttpMessage {
public:
    http_status status_code;
    const char* status_message() {
        return http_status_str(status_code);
    }

    HttpResponse() : HttpMessage() {
        type = HTTP_RESPONSE;
        Init();
    }

    void Init() {
        status_code = HTTP_STATUS_OK;
    }

    virtual void Reset() {
        HttpMessage::Reset();
        Init();
    }

    virtual std::string Dump(bool is_dump_headers = true, bool is_dump_body = false);
};

typedef std::shared_ptr<HttpRequest>    HttpRequestPtr;
typedef std::shared_ptr<HttpResponse>   HttpResponsePtr;
typedef std::function<void(const HttpResponsePtr&)> HttpResponseCallback;

#endif // HTTP_MESSAGE_H_
