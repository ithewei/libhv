#ifndef HTTP_REQUEST_H_
#define HTTP_REQUEST_H_

#include <string.h>

#include <string>
#include <map>

// for http_method, http_status
#include "http_parser.h"
inline http_method http_method_enum(const char* str) {
#define XX(num, name, string) \
    if (strcmp(str, #string) == 0) { \
        return HTTP_##name; \
    }
    HTTP_METHOD_MAP(XX)
#undef XX
    return HTTP_GET;
}

// http_content_type
// XX(name, string, suffix)
#define HTTP_CONTENT_TYPE_MAP(XX) \
    XX(TEXT_PLAIN,              "text/plain",               "txt")   \
    XX(TEXT_HTML,               "text/html",                "html")    \
    XX(TEXT_CSS,                "text/css",                 "css")     \
    XX(APPLICATION_JAVASCRIPT,  "application/javascript",   "js")   \
    XX(APPLICATION_XML,         "application/xml",          "xml")  \
    XX(APPLICATION_JSON,        "application/json",         "json") \
    XX(X_WWW_FORM_URLENCODED,   "application/x-www-form-urlencoded", ".null.") \
    XX(MULTIPART_FORM_DATA,     "multipart/form-data",               ".null.") \
    XX(IMAGE_JPEG,              "image/jpeg",               "jpg") \
    XX(IMAGE_PNG,               "image/png",                "png") \
    XX(IMAGE_gif,               "image/gif",                "gif")

enum http_content_type {
#define XX(name, string, suffix)   name,
    CONTENT_TYPE_NONE,
    HTTP_CONTENT_TYPE_MAP(XX)
    CONTENT_TYPE_UNDEFINED
#undef XX
};

inline const char* http_content_type_str(enum http_content_type type) {
    switch (type) {
#define XX(name, string, suffix) \
    case name:  return string;
    HTTP_CONTENT_TYPE_MAP(XX)
    default:    return "";
#undef XX
    }
}
// replace strncmp(s1, s2, strlen(s2))
inline int mystrcmp(const char* s1, const char* s2) {
    while (*s1 != '\0' && *s2 != '\0' && *s1 == *s2) {++s1;++s2;}
    return *s2 == 0 ? 0 : (*s1-*s2);
}
inline enum http_content_type http_content_type_enum(const char* str) {
#define XX(name, string, suffix) \
    if (mystrcmp(str, string) == 0) { \
        return name; \
    }
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    return CONTENT_TYPE_UNDEFINED;
}

inline enum http_content_type http_content_type_enum_by_suffix(const char* suf) {
#define XX(name, string, suffix) \
    if (strcmp(suf, suffix) == 0) { \
        return name; \
    }
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    return CONTENT_TYPE_UNDEFINED;
}

inline const char* http_content_type_str_by_suffix(const char* suf) {
#define XX(name, string, suffix) \
    if (strcmp(suf, suffix) == 0) { \
        return string; \
    }
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    return "";
}

#include "http_content.h"
#include "hstring.h"
typedef std::map<std::string, std::string, StringCaseLess>  http_headers;
typedef std::string     http_body;
class HttpInfo {
public:
    unsigned short      http_major;
    unsigned short      http_minor;
    http_headers        headers;
    http_body           body;
    // parsed content
    http_content_type   content_type;
    Json                json;       // APPLICATION_JSON
    MultiPart           mp;         // FORM_DATA
    KeyValue            kv;         // X_WWW_FORM_URLENCODED

    HttpInfo() {
        http_major = 1;
        http_minor = 1;
        content_type = CONTENT_TYPE_NONE;
    }

    void fill_content_type() {
        auto iter = headers.find("Content-Type");
        if (iter != headers.end()) {
            content_type = http_content_type_enum(iter->second.c_str());
            goto append;
        }

        if (content_type == CONTENT_TYPE_NONE) {
            if (json.size() != 0) {
                content_type = APPLICATION_JSON;
            }
            else if (mp.size() != 0) {
                content_type = MULTIPART_FORM_DATA;
            }
            else if (kv.size() != 0) {
                content_type = X_WWW_FORM_URLENCODED;
            }
            else if (body.size() != 0) {
                content_type = TEXT_PLAIN;
            }
        }

        if (content_type != CONTENT_TYPE_NONE) {
            headers["Content-Type"] = http_content_type_str(content_type);
        }
append:
        if (content_type == MULTIPART_FORM_DATA) {
            auto iter = headers.find("Content-Type");
            if (iter != headers.end()) {
                const char* boundary = strstr(iter->second.c_str(), "boundary=");
                if (boundary == NULL) {
                    boundary = DEFAULT_MULTIPART_BOUNDARY;
                    iter->second += "; boundary=";
                    iter->second += boundary;
                }
            }
        }
    }

    void fill_content_lenght() {
        if (body.size() != 0) {
            headers["Content-Length"] = std::to_string(body.size());
        }
    }

    void dump_headers(std::string& str) {
        fill_content_type();
        fill_content_lenght();
        for (auto& header: headers) {
            // %s: %s\r\n
            str += header.first;
            str += ": ";
            str += header.second;
            str += "\r\n";
        }
    }

    void dump_body() {
        if (body.size() != 0) {
            return;
        }
        fill_content_type();
        switch(content_type) {
        case APPLICATION_JSON:
            body = dump_json(json);
            break;
        case MULTIPART_FORM_DATA:
        {
            auto iter = headers.find("Content-Type");
            if (iter == headers.end()) {
                return;
            }
            const char* boundary = strstr(iter->second.c_str(), "boundary=");
            if (boundary == NULL) {
                return;
            }
            boundary += strlen("boundary=");
            body = dump_multipart(mp, boundary);
        }
            break;
        case X_WWW_FORM_URLENCODED:
            body = dump_query_params(kv);
            break;
        default:
            // nothing to do
            break;
        }
    }

    bool parse_body() {
        if (body.size() == 0) {
            return false;
        }
        fill_content_type();
        switch(content_type) {
        case APPLICATION_JSON:
            parse_json(body.c_str(), json);
            break;
        case MULTIPART_FORM_DATA:
        {
            auto iter = headers.find("Content-Type");
            if (iter == headers.end()) {
                return false;
            }
            const char* boundary = strstr(iter->second.c_str(), "boundary=");
            if (boundary == NULL) {
                return false;
            }
            boundary += strlen("boundary=");
            parse_multipart(body, mp, boundary);
        }
            break;
        case X_WWW_FORM_URLENCODED:
            parse_query_params(body.c_str(), kv);
            break;
        default:
            // nothing to do
            break;
        }
        return true;
    }
};

#define DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36"
class HttpRequest : public HttpInfo {
public:
    http_method         method;
    // scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
    std::string         url;
    QueryParams         query_params;

    HttpRequest() {
        method = HTTP_GET;
        headers["User-Agent"] = DEFAULT_USER_AGENT;
        headers["Accept"] = "*/*";
    }

    std::string dump_url() {
        std::string str;
        if (strstr(url.c_str(), "://") == NULL) {
            str += "http://";
        }
        if (*url.c_str() == '/') {
            str += headers["Host"];
        }
        str += url;
        if (strchr(url.c_str(), '?') || query_params.size() == 0) {
            return str;
        }
        str += '?';
        str += dump_query_params(query_params);
        return str;
    }

    void parse_url() {
        if (query_params.size() != 0) {
            return;
        }
        const char* token = strchr(url.c_str(), '?');
        if (token == NULL) {
            return;
        }
        parse_query_params(token+1, query_params);
    }

    std::string dump(bool is_dump_headers = true, bool is_dump_body = false) {
        char c_str[256] = {0};
        const char* path = "/";
        if (*url.c_str() == '/') {
            path = url.c_str();
        }
        else {
            std::string url = dump_url();
            http_parser_url parser;
            http_parser_url_init(&parser);
            http_parser_parse_url(url.c_str(), url.size(), 0, &parser);
            if (parser.field_set & (1<<UF_HOST)) {
                std::string host = url.substr(parser.field_data[UF_HOST].off, parser.field_data[UF_HOST].len);
                int port = parser.port;
                if (port == 0) {
                    headers["Host"] = host;
                }
                else {
                    snprintf(c_str, sizeof(c_str), "%s:%d", host.c_str(), port);
                    headers["Host"] = c_str;
                }
            }
            if (parser.field_set & (1<<UF_PATH)) {
                path = url.c_str() + parser.field_data[UF_PATH].off;
            }
        }

        std::string str;
        // GET / HTTP/1.1\r\n
        snprintf(c_str, sizeof(c_str), "%s %s HTTP/%d.%d\r\n", http_method_str(method), path, http_major, http_minor);
        str += c_str;
        if (is_dump_headers) {
            dump_headers(str);
        }
        str += "\r\n";
        if (is_dump_body) {
            dump_body();
            str += body;
        }
        return str;
    }
};

class HttpResponse : public HttpInfo {
public:
    http_status         status_code;

    HttpResponse() {
        status_code = HTTP_STATUS_OK;
    }

    std::string dump(bool is_dump_headers = true, bool is_dump_body = false) {
        char c_str[256] = {0};
        std::string str;
        // HTTP/1.1 200 OK\r\n
        snprintf(c_str, sizeof(c_str), "HTTP/%d.%d %d %s\r\n", http_major, http_minor, status_code, http_status_str(status_code));
        str += c_str;
        if (is_dump_headers) {
            dump_headers(str);
        }
        str += "\r\n";
        if (is_dump_body) {
            dump_body();
            str += body;
        }
        return str;
    }
};

#endif // HTTP_REQUEST_H_

