#ifndef HV_HTTP_MESSAGE_H_
#define HV_HTTP_MESSAGE_H_

/*
 * @class HttpMessage
 * HttpRequest extends HttpMessage
 * HttpResponse extends HttpMessage
 *
 * @member
 * request-line:  GET / HTTP/1.1\r\n => method path
 * response-line: HTTP/1.1 200 OK\r\n => status_code
 * headers, cookies
 * body
 *
 * content, content_length, content_type
 * json, form, kv
 *
 * @function
 * Content, ContentLength, ContentType
 * ParseUrl, ParseBody
 * DumpUrl, DumpHeaders, DumpBody, Dump
 * GetHeader, GetParam, GetJson, GetFormData, GetUrlEncoded
 * SetHeader, SetParam, SetBody, SetFormData, SetUrlEncoded
 * Get<T>, Set<T>
 * GetString, GetBool, GetInt, GetFloat
 * String, Data, Json, File, FormFile
 *
 * @example
 * examples/http_server_test.cpp
 * examples/http_client_test.cpp
 * examples/httpd
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
#include "hpath.h"

#include "httpdef.h"
#include "http_content.h"

namespace hv {

struct NetAddr {
    std::string     ip;
    int             port;

    std::string ipport() {
        return hv::asprintf("%s:%d", ip.c_str(), port);
    }
};

}

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Set-Cookie
// Cookie: sessionid=1; domain=.example.com; path=/; max-age=86400; secure; httponly
struct HV_EXPORT HttpCookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    std::string expires;
    int         max_age;
    bool        secure;
    bool        httponly;
    enum SameSite {
        Default,
        Strict,
        Lax,
        None
    } samesite;
    enum Priority {
        NotSet,
        Low,
        Medium,
        High,
    } priority;
    hv::KeyValue kv; // for multiple names

    HttpCookie() {
        init();
    }

    void init()  {
        max_age = 0;
        secure = false;
        httponly = false;
        samesite = Default;
        priority = NotSet;
    }

    void reset() {
        init();
        name.clear();
        value.clear();
        domain.clear();
        path.clear();
        expires.clear();
        kv.clear();
    }

    bool parse(const std::string& str);
    std::string dump() const;
};

typedef std::map<std::string, std::string, hv::StringCaseLess>  http_headers;
typedef std::vector<HttpCookie>                                 http_cookies;
typedef std::string                                             http_body;

HV_EXPORT extern http_headers DefaultHeaders;
HV_EXPORT extern http_body    NoBody;
HV_EXPORT extern HttpCookie   NoCookie;

class HV_EXPORT HttpMessage {
public:
    static char         s_date[32];
    int                 type;
    unsigned short      http_major;
    unsigned short      http_minor;

    http_headers        headers;
    http_cookies        cookies;
    http_body           body;

    // http_cb
    std::function<void(HttpMessage*, http_parser_state state, const char* data, size_t size)> http_cb;

    // structured content
    void*               content;    // DATA_NO_COPY
    size_t              content_length;
    http_content_type   content_type;
#ifndef WITHOUT_HTTP_CONTENT
    hv::Json            json;       // APPLICATION_JSON
    hv::MultiPart       form;       // MULTIPART_FORM_DATA
    hv::KeyValue        kv;         // X_WWW_FORM_URLENCODED

    // T=[bool, int, int64_t, float, double]
    template<typename T>
    T Get(const char* key, T defvalue = 0);

    std::string GetString(const char* key, const std::string& = "");
    bool GetBool(const char* key, bool defvalue = 0);
    int64_t GetInt(const char* key, int64_t defvalue = 0);
    double GetFloat(const char* key, double defvalue = 0);

    template<typename T>
    void Set(const char* key, const T& value) {
        switch (ContentType()) {
        case APPLICATION_JSON:
            json[key] = value;
            break;
        case MULTIPART_FORM_DATA:
            form[key] = hv::FormData(value);
            break;
        case X_WWW_FORM_URLENCODED:
            kv[key] = hv::to_string(value);
            break;
        default:
            break;
        }
    }

    /*
     * @usage   https://github.com/nlohmann/json
     *
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
                Json(hv::Json::array(
                    {1, 2, 3}
                ));
     */
    // Content-Type: application/json
    template<typename T>
    int Json(const T& t) {
        content_type = APPLICATION_JSON;
        hv::Json j(t);
        body = j.dump(2);
        return 200;
    }
    const hv::Json& GetJson() {
        if (json.empty() && ContentType() == APPLICATION_JSON) {
            ParseBody();
        }
        return json;
    }

    // Content-Type: multipart/form-data
    template<typename T>
    void SetFormData(const char* name, const T& t) {
        form[name] = hv::FormData(t);
    }
    void SetFormFile(const char* name, const char* filepath) {
        form[name] = hv::FormData(NULL, filepath);
    }
    int FormFile(const char* name, const char* filepath) {
        content_type = MULTIPART_FORM_DATA;
        form[name] = hv::FormData(NULL, filepath);
        return 200;
    }
    const hv::MultiPart& GetForm() {
        if (form.empty() && ContentType() == MULTIPART_FORM_DATA) {
            ParseBody();
        }
        return form;
    }
    std::string GetFormData(const char* name, const std::string& defvalue = hv::empty_string) {
        if (form.empty() && ContentType() == MULTIPART_FORM_DATA) {
            ParseBody();
        }
        auto iter = form.find(name);
        return iter == form.end() ? defvalue : iter->second.content;
    }
    int SaveFormFile(const char* name, const char* path) {
        if (ContentType() != MULTIPART_FORM_DATA) {
            return HTTP_STATUS_BAD_REQUEST;
        }
        if (form.empty()) {
            ParseBody();
            if (form.empty()) return HTTP_STATUS_BAD_REQUEST;
        }
        auto iter = form.find(name);
        if (iter == form.end()) {
            return HTTP_STATUS_BAD_REQUEST;
        }
        const auto& formdata = iter->second;
        if (formdata.content.empty()) {
            return HTTP_STATUS_BAD_REQUEST;
        }
        std::string filepath(path);
        if (HPath::isdir(path)) {
            filepath = HPath::join(filepath, formdata.filename);
        }
        HFile file;
        if (file.open(filepath.c_str(), "wb") != 0) {
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
        }
        file.write(formdata.content.data(), formdata.content.size());
        return 200;
    }

    // Content-Type: application/x-www-form-urlencoded
    template<typename T>
    void SetUrlEncoded(const char* key, const T& t) {
        kv[key] = hv::to_string(t);
    }
    const hv::KeyValue& GetUrlEncoded() {
        if (kv.empty() && ContentType() == X_WWW_FORM_URLENCODED) {
            ParseBody();
        }
        return kv;
    }
    std::string GetUrlEncoded(const char* key, const std::string& defvalue = hv::empty_string) {
        if (kv.empty() && ContentType() == X_WWW_FORM_URLENCODED) {
            ParseBody();
        }
        auto iter = kv.find(key);
        return iter == kv.end() ? defvalue : iter->second;
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
        cookies.clear();
        body.clear();
#ifndef WITHOUT_HTTP_CONTENT
        json.clear();
        form.clear();
        kv.clear();
#endif
    }

    // structured-content -> content_type <-> headers["Content-Type"]
    void FillContentType();
    // body.size -> content_length <-> headers["Content-Length"]
    void FillContentLength();

    bool IsChunked();
    bool IsKeepAlive();

    // headers
    void SetHeader(const char* key, const std::string& value) {
        headers[key] = value;
    }
    std::string GetHeader(const char* key, const std::string& defvalue = hv::empty_string) {
        auto iter = headers.find(key);
        return iter == headers.end() ? defvalue : iter->second;
    }

    // body
    void SetBody(const std::string& body) {
        this->body = body;
    }
    const std::string& Body() {
        return this->body;
    }

    // headers -> string
    void DumpHeaders(std::string& str);
    // structured content -> body
    void DumpBody();
    void DumpBody(std::string& str);
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

    size_t ContentLength() {
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
    void SetContentType(http_content_type type) {
        content_type = type;
    }
    void SetContentType(const char* type) {
        content_type = http_content_type_enum(type);
    }
    void SetContentTypeByFilename(const char* filepath) {
        const char* suffix = hv_suffixname(filepath);
        if (suffix) {
            content_type = http_content_type_enum_by_suffix(suffix);
        }
        if (content_type == CONTENT_TYPE_NONE || content_type == CONTENT_TYPE_UNDEFINED) {
            content_type = APPLICATION_OCTET_STREAM;
        }
    }

    void AddCookie(const HttpCookie& cookie) {
        cookies.push_back(cookie);
    }

    const HttpCookie& GetCookie(const std::string& name) {
        for (auto iter = cookies.begin(); iter != cookies.end(); ++iter) {
            if (iter->name == name) {
                return *iter;
            }
        }
        return NoCookie;
    }

    int String(const std::string& str) {
        content_type = TEXT_PLAIN;
        body = str;
        return 200;
    }

    int Data(void* data, int len, bool nocopy = true) {
        content_type = APPLICATION_OCTET_STREAM;
        if (nocopy) {
            content = data;
            content_length = len;
        } else {
            content_length = body.size();
            body.resize(content_length + len);
            memcpy((void*)(body.data() + content_length), data, len);
            content_length += len;
        }
        return 200;
    }

    int File(const char* filepath) {
        HFile file;
        if (file.open(filepath, "rb") != 0) {
            return HTTP_STATUS_NOT_FOUND;
        }
        SetContentTypeByFilename(filepath);
        file.readall(body);
        return 200;
    }

    int SaveFile(const char* filepath) {
        HFile file;
        if (file.open(filepath, "wb") != 0) {
            return HTTP_STATUS_NOT_FOUND;
        }
        file.write(body.data(), body.size());
        return 200;
    }
};

#define DEFAULT_HTTP_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36"
#define DEFAULT_HTTP_TIMEOUT            60 // s
#define DEFAULT_HTTP_CONNECT_TIMEOUT    10 // s
#define DEFAULT_HTTP_FAIL_RETRY_COUNT   1
#define DEFAULT_HTTP_FAIL_RETRY_DELAY   1000 // ms

class HV_EXPORT HttpRequest : public HttpMessage {
public:
    http_method         method;
    // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
    std::string         url;
    // structured url
    std::string         scheme;
    std::string         host;
    int                 port;
    std::string         path;
    hv::QueryParams     query_params;
    // client_addr
    hv::NetAddr         client_addr; // for http server save client addr of request
    // for HttpClient
    uint16_t            timeout;        // unit: s
    uint16_t            connect_timeout;// unit: s
    uint32_t            retry_count;    // just for AsyncHttpClient fail retry
    uint32_t            retry_delay;    // just for AsyncHttpClient fail retry
    unsigned            redirect: 1;
    unsigned            proxy   : 1;

    HttpRequest() : HttpMessage() {
        type = HTTP_REQUEST;
        Init();
    }

    void Init() {
        headers["User-Agent"] = DEFAULT_HTTP_USER_AGENT;
        headers["Accept"] = "*/*";
        method = HTTP_GET;
        scheme = "http";
        host = "127.0.0.1";
        port = DEFAULT_HTTP_PORT;
        path = "/";
        timeout = DEFAULT_HTTP_TIMEOUT;
        connect_timeout = DEFAULT_HTTP_CONNECT_TIMEOUT;
        retry_count = DEFAULT_HTTP_FAIL_RETRY_COUNT;
        retry_delay = DEFAULT_HTTP_FAIL_RETRY_DELAY;
        redirect = 1;
        proxy = 0;
    }

    virtual void Reset() {
        HttpMessage::Reset();
        Init();
        url.clear();
        query_params.clear();
    }

    virtual std::string Dump(bool is_dump_headers = true, bool is_dump_body = false);

    // method
    void SetMethod(const char* method) {
        this->method = http_method_enum(method);
    }
    const char* Method() {
        return http_method_str(method);
    }

    // scheme
    bool IsHttps() {
        return strncmp(scheme.c_str(), "https", 5) == 0 ||
               strncmp(url.c_str(), "https://", 8) == 0;
    }

    // url
    void SetUrl(const char* url) {
        this->url = url;
    }
    const std::string& Url() {
        return url;
    }
    // structed url -> url
    void DumpUrl();
    // url -> structed url
    void ParseUrl();

    // /path?query#fragment
    std::string FullPath() { return path; }
    // /path
    std::string Path();

    // ?query_params
    template<typename T>
    void SetParam(const char* key, const T& t) {
        query_params[key] = hv::to_string(t);
    }
    std::string GetParam(const char* key, const std::string& defvalue = hv::empty_string) {
        auto iter = query_params.find(key);
        return iter == query_params.end() ? defvalue : iter->second;
    }

    // Host:
    std::string Host() {
        auto iter = headers.find("Host");
        return iter == headers.end() ? host : iter->second;
    }
    void FillHost(const char* host, int port = DEFAULT_HTTP_PORT);
    void SetHost(const char* host, int port = DEFAULT_HTTP_PORT);

    void SetProxy(const char* host, int port);
    bool IsProxy() { return proxy; }

    void SetTimeout(int sec) { timeout = sec; }
    void SetConnectTimeout(int sec) { connect_timeout = sec; }

    void AllowRedirect(bool on = true) { redirect = on; }

    // NOTE: SetRetry just for AsyncHttpClient
    void SetRetry(int count = DEFAULT_HTTP_FAIL_RETRY_COUNT,
                  int delay = DEFAULT_HTTP_FAIL_RETRY_DELAY) {
        retry_count = count;
        retry_delay = delay;
    }

    // Range: bytes=0-4095
    void SetRange(long from = 0, long to = -1) {
        headers["Range"] = hv::asprintf("bytes=%ld-%ld", from, to);
    }
    bool GetRange(long& from, long& to) {
        auto iter = headers.find("Range");
        if (iter != headers.end()) {
            sscanf(iter->second.c_str(), "bytes=%ld-%ld", &from, &to);
            return true;
        }
        from = to = 0;
        return false;
    }
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

    // Content-Range: bytes 0-4095/10240000
    void SetRange(long from, long to, long total) {
        headers["Content-Range"] = hv::asprintf("bytes %ld-%ld/%ld", from, to, total);
    }
    bool GetRange(long& from, long& to, long& total) {
        auto iter = headers.find("Content-Range");
        if (iter != headers.end()) {
            sscanf(iter->second.c_str(), "bytes %ld-%ld/%ld", &from, &to, &total);
            return true;
        }
        from = to = total = 0;
        return false;
    }

    int Redirect(const std::string& location, http_status status = HTTP_STATUS_FOUND) {
        status_code = status;
        headers["Location"] = location;
        return status_code;
    }
};

typedef std::shared_ptr<HttpRequest>    HttpRequestPtr;
typedef std::shared_ptr<HttpResponse>   HttpResponsePtr;
typedef std::function<void(const HttpResponsePtr&)> HttpResponseCallback;

#endif // HV_HTTP_MESSAGE_H_
