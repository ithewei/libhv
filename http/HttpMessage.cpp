#include "HttpMessage.h"

#include <string.h>

#include "htime.h"
#include "hlog.h"
#include "http_parser.h" // for http_parser_url

#ifndef WITHOUT_HTTP_CONTENT
// NOTE: json ignore number/string, 123/"123"

std::string HttpMessage::GetString(const char* key, const std::string& defvalue) {
    switch (content_type) {
    case APPLICATION_JSON:
    {
        auto value = json[key];
        if (value.is_string()) {
            return value;
        }
        else if (value.is_number()) {
            return hv::to_string(value);
        }
        else if (value.is_null()) {
            return "null";
        }
        else if (value.is_boolean()) {
            bool b = value;
            return b ? "true" : "false";
        }
        else {
            return defvalue;
        }
    }
        break;
    case MULTIPART_FORM_DATA:
    {
        auto iter = form.find(key);
        if (iter != form.end()) {
            return iter->second.content;
        }
    }
        break;
    case APPLICATION_URLENCODED:
    {
        auto iter = kv.find(key);
        if (iter != kv.end()) {
            return iter->second;
        }
    }
        break;
    default:
        break;
    }
    return defvalue;
}

template<>
HV_EXPORT int64_t HttpMessage::Get(const char* key, int64_t defvalue) {
    if (content_type == APPLICATION_JSON) {
        auto value = json[key];
        if (value.is_number()) {
            return value;
        }
        else if (value.is_string()) {
            std::string str = value;
            return atoll(str.c_str());
        }
        else if (value.is_null()) {
            return 0;
        }
        else if (value.is_boolean()) {
            bool b = value;
            return b ? 1 : 0;
        }
        else {
            return defvalue;
        }
    }
    else {
        std::string str = GetString(key);
        return str.empty() ? defvalue : atoll(str.c_str());
    }
}

template<>
HV_EXPORT double HttpMessage::Get(const char* key, double defvalue) {
    if (content_type == APPLICATION_JSON) {
        auto value = json[key];
        if (value.is_number()) {
            return value;
        }
        else if (value.is_string()) {
            std::string str = value;
            return atof(str.c_str());
        }
        else if (value.is_null()) {
            return 0.0f;
        }
        else {
            return defvalue;
        }
    }
    else {
        std::string str = GetString(key);
        return str.empty() ? defvalue : atof(str.c_str());
    }
}

template<>
HV_EXPORT bool HttpMessage::Get(const char* key, bool defvalue) {
    if (content_type == APPLICATION_JSON) {
        auto value = json[key];
        if (value.is_boolean()) {
            return value;
        }
        else if (value.is_string()) {
            std::string str = value;
            return getboolean(str.c_str());
        }
        else if (value.is_null()) {
            return false;
        }
        else if (value.is_number()) {
            return value != 0;
        }
        else {
            return defvalue;
        }
    }
    else {
        std::string str = GetString(key);
        return str.empty() ? defvalue : getboolean(str.c_str());
    }
}

bool HttpMessage::GetBool(const char* key, bool defvalue) {
    return Get<bool>(key, defvalue);
}
int64_t HttpMessage::GetInt(const char* key, int64_t defvalue) {
    return Get<int64_t>(key, defvalue);
}
double HttpMessage::GetFloat(const char* key, double defvalue) {
    return Get<double>(key, defvalue);
}
#endif

void HttpMessage::FillContentType() {
    auto iter = headers.find("Content-Type");
    if (iter != headers.end()) {
        content_type = http_content_type_enum(iter->second.c_str());
        goto append;
    }

#ifndef WITHOUT_HTTP_CONTENT
    if (content_type == CONTENT_TYPE_NONE) {
        if (json.size() != 0) {
            content_type = APPLICATION_JSON;
        }
        else if (form.size() != 0) {
            content_type = MULTIPART_FORM_DATA;
        }
        else if (kv.size() != 0) {
            content_type = X_WWW_FORM_URLENCODED;
        }
        else if (body.size() != 0) {
            content_type = TEXT_PLAIN;
        }
    }
#endif

    if (content_type != CONTENT_TYPE_NONE) {
        headers["Content-Type"] = http_content_type_str(content_type);
    }

append:
#ifndef WITHOUT_HTTP_CONTENT
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
#endif
    return;
}

void HttpMessage::FillContentLength() {
    auto iter = headers.find("Content-Length");
    if (iter != headers.end()) {
        content_length = atoi(iter->second.c_str());
    }

    if (iter == headers.end() || content_length == 0) {
        if (content_length == 0) {
            content_length = body.size();
        }
        if (content_length == 0) {
            DumpBody();
            content_length = body.size();
        }
        char sz[64];
        snprintf(sz, sizeof(sz), "%d", content_length);
        headers["Content-Length"] = sz;
    }
}

void HttpMessage::DumpHeaders(std::string& str) {
    FillContentType();
    FillContentLength();
    for (auto& header: headers) {
        // http2 :method :path :scheme :authority :status
        if (*str.c_str() != ':') {
            // %s: %s\r\n
            str += header.first;
            str += ": ";
            str += header.second;
            str += "\r\n";
        }
    }
}

void HttpMessage::DumpBody() {
    if (body.size() != 0) {
        return;
    }
    FillContentType();
#ifndef WITHOUT_HTTP_CONTENT
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
        body = dump_multipart(form, boundary);
    }
        break;
    case X_WWW_FORM_URLENCODED:
        body = dump_query_params(kv);
        break;
    default:
        // nothing to do
        break;
    }
#endif
}

int HttpMessage::ParseBody() {
    if (body.size() == 0) {
        return -1;
    }
    FillContentType();
#ifndef WITHOUT_HTTP_CONTENT
    switch(content_type) {
    case APPLICATION_JSON:
    {
        std::string errmsg;
        int ret = parse_json(body.c_str(), json, errmsg);
        if (ret != 0 && errmsg.size() != 0) {
            hloge("%s", errmsg.c_str());
        }
        return ret;
    }
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
        string strBoundary(boundary);
        strBoundary = trim_pairs(strBoundary, "\"\"\'\'");
        return parse_multipart(body, form, strBoundary.c_str());
    }
    case X_WWW_FORM_URLENCODED:
        return parse_query_params(body.c_str(), kv);
    default:
        // nothing to do
        return 0;
    }
#endif
    return 0;
}

std::string HttpMessage::Dump(bool is_dump_headers, bool is_dump_body) {
    std::string str;
    if (is_dump_headers) {
        DumpHeaders(str);
    }
    str += "\r\n";
    if (is_dump_body) {
        DumpBody();
        if (ContentLength() != 0) {
            str.insert(str.size(), (const char*)Content(), ContentLength());
        }
    }
    return str;
}

void HttpRequest::DumpUrl() {
    if (url.size() != 0 && strncmp(url.c_str(), "http", 4) == 0) {
        // have been complete url
        return;
    }
    std::string str;
    // scheme://
    str += "http";
    if (https) str += 's';
    str += "://";
    // host:port
    char c_str[256] = {0};
    if (url.size() != 0 && *url.c_str() != '/') {
        // url begin with host
        str += url;
    }
    else {
        if (port == 0 ||
            port == DEFAULT_HTTP_PORT ||
            port == DEFAULT_HTTPS_PORT) {
            str += host;
        }
        else {
            snprintf(c_str, sizeof(c_str), "%s:%d", host.c_str(), port);
            str += c_str;
        }
    }
    // /path
    if (url.size() != 0 && *url.c_str() == '/') {
        // url begin with path
        str += url;
    }
    else if (path.size() > 1 && *path.c_str() == '/') {
        str += path;
    }
    else if (url.size() == 0) {
        str += '/';
    }
    // ?query
    if (strchr(str.c_str(), '?') == NULL &&
        query_params.size() != 0) {
        str += '?';
        str += dump_query_params(query_params);
    }
    url = str;
}

void HttpRequest::ParseUrl() {
    DumpUrl();
    http_parser_url parser;
    http_parser_url_init(&parser);
    http_parser_parse_url(url.c_str(), url.size(), 0, &parser);
    // scheme
    https = !strncmp(url.c_str(), "https", 5);
    // host
    if (parser.field_set & (1<<UF_HOST)) {
        host = url.substr(parser.field_data[UF_HOST].off, parser.field_data[UF_HOST].len);
    }
    // port
    port = parser.port ? parser.port : https ? DEFAULT_HTTPS_PORT : DEFAULT_HTTP_PORT;
    // path
    if (parser.field_set & (1<<UF_PATH)) {
        path = url.c_str() + parser.field_data[UF_PATH].off;
    }
    // query
    if (parser.field_set & (1<<UF_QUERY)) {
        parse_query_params(url.c_str()+parser.field_data[UF_QUERY].off, query_params);
    }
}

std::string HttpRequest::Dump(bool is_dump_headers, bool is_dump_body) {
    ParseUrl();

    char c_str[256] = {0};
    std::string str;
    // GET / HTTP/1.1\r\n
    snprintf(c_str, sizeof(c_str), "%s %s HTTP/%d.%d\r\n", http_method_str(method), path.c_str(), http_major, http_minor);
    str += c_str;
    if (is_dump_headers) {
        // Host:
        if (headers.find("Host") == headers.end()) {
            if (port == 0 ||
                port == DEFAULT_HTTP_PORT ||
                port == DEFAULT_HTTPS_PORT) {
                headers["Host"] = host;
            }
            else {
                snprintf(c_str, sizeof(c_str), "%s:%d", host.c_str(), port);
                headers["Host"] = c_str;
            }
        }
        DumpHeaders(str);
    }
    str += "\r\n";
    if (is_dump_body) {
        DumpBody();
        if (ContentLength() != 0) {
            str.insert(str.size(), (const char*)Content(), ContentLength());
        }
    }
    return str;
}

std::string HttpResponse::Dump(bool is_dump_headers, bool is_dump_body) {
    char c_str[256] = {0};
    std::string str;
    // HTTP/1.1 200 OK\r\n
    snprintf(c_str, sizeof(c_str), "HTTP/%d.%d %d %s\r\n", http_major, http_minor, status_code, http_status_str(status_code));
    str += c_str;
    if (is_dump_headers) {
        headers["Date"] = gmtime_fmt(time(NULL), c_str);
        DumpHeaders(str);
    }
    str += "\r\n";
    if (is_dump_body) {
        DumpBody();
        if (ContentLength() != 0) {
            str.insert(str.size(), (const char*)Content(), ContentLength());
        }
    }
    return str;
}
