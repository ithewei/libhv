#ifndef HV_HTTP_MESSAGE_H_
#define HV_HTTP_MESSAGE_H_

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

struct HNetAddr {
    std::string     ip;
    int             port;

    std::string ipport() {
        return asprintf("%s:%d", ip.c_str(), port);
    }
};

// Cookie: sessionid=1; domain=.example.com; path=/; max-age=86400; secure; httponly
struct HV_EXPORT HttpCookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    int         max_age;
    bool        secure;
    bool        httponly;

    HttpCookie() {
        max_age = 86400;
        secure = false;
        httponly = false;
    }

    bool parse(const std::string& str);
    std::string dump() const;
};

typedef std::map<std::string, std::string, StringCaseLess>  http_headers;
typedef std::vector<HttpCookie>                             http_cookies;
typedef std::string                                         http_body;

class HV_EXPORT HttpMessage {
public:
    static char         s_date[32];
    int                 type;
    unsigned short      http_major;
    unsigned short      http_minor;

    http_headers        headers;
    http_cookies        cookies;
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
        const FormData& formdata = form[name];
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

    bool IsKeepAlive();

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
    std::string         scheme;
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
        scheme = "http";
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

    // structed url -> url
    void DumpUrl();
    // url -> structed url
    void ParseUrl();

    char CharToInt(char ch) 
    {
        if (ch >= '0' && ch <= '9')return (char)(ch - '0');
        if (ch >= 'a' && ch <= 'f')return (char)(ch - 'a' + 10);
        if (ch >= 'A' && ch <= 'F')return (char)(ch - 'A' + 10);
        return -1;
    }

    char StrToBin(const char *str) 
    {
        char tempWord[2];
        char chn;
        tempWord[0] = CharToInt(str[0]); //make the B to 11 -- 00001011 
        tempWord[1] = CharToInt(str[1]); //make the 0 to 0 -- 00000000 
        chn = (tempWord[0] << 4) | tempWord[1]; //to change the BO to 10110000 
        return chn;
    }

    void DecodeUrl()
    {
        std::string output = "";
        char tmp[2];
        int i = 0, len = url.length();
        while (i < len) {
            if (url[i] == '%') {
                if(i > len - 3){
                    //防止内存溢出
                    break;
                }
                tmp[0] = url[i + 1];
                tmp[1] = url[i + 2];
                output += StrToBin(tmp);
                i = i + 3;
            } else if (url[i] == '+') {
                output += ' ';
                i++;
            } else {
                output += url[i];
                i++;
            }
        }
        url = output;
    }
    
    std::string Host() {
        auto iter = headers.find("Host");
        if (iter != headers.end()) {
            host = iter->second;
        }
        return host;
    }

    std::string Path() {
        const char* s = path.c_str();
        const char* e = s;
        while (*e && *e != '?' && *e != '#') ++e;
        return std::string(s, e);
    }

    std::string GetParam(const char* key, const std::string& defvalue = "") {
        auto iter = query_params.find(key);
        if (iter != query_params.end()) {
            return iter->second;
        }
        return defvalue;
    }

    // Range: bytes=0-4095
    void SetRange(long from = 0, long to = -1) {
        headers["Range"] = asprintf("bytes=%ld-%ld", from, to);
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

    // Cookie
    void SetCookie(const HttpCookie& cookie) {
        headers["Cookie"] = cookie.dump();
    }
    bool GetCookie(HttpCookie& cookie) {
        std::string str = GetHeader("Cookie");
        if (str.empty()) return false;
        return cookie.parse(str);
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
        headers["Content-Range"] = asprintf("bytes %ld-%ld/%ld", from, to, total);
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

    // Set-Cookie
    void SetCookie(const HttpCookie& cookie) {
        headers["Set-Cookie"] = cookie.dump();
    }
    bool GetCookie(HttpCookie& cookie) {
        std::string str = GetHeader("Set-Cookie");
        if (str.empty()) return false;
        return cookie.parse(str);
    }
};

typedef std::shared_ptr<HttpRequest>    HttpRequestPtr;
typedef std::shared_ptr<HttpResponse>   HttpResponsePtr;
typedef std::function<void(const HttpResponsePtr&)> HttpResponseCallback;

#endif // HV_HTTP_MESSAGE_H_
