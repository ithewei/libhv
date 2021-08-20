#include "HttpMessage.h"

#include <string.h>

#include "htime.h"
#include "hlog.h"
#include "hurl.h"
#include "http_parser.h" // for http_parser_url

char HttpMessage::s_date[32] = {0};

bool HttpCookie::parse(const std::string& str) {
    std::stringstream ss;
    ss << str;
    std::string kv;
    std::string::size_type pos;
    std::string key;
    std::string val;
    while (std::getline(ss, kv, ';')) {
        pos = kv.find_first_of('=');
        if (pos != std::string::npos) {
            key = trim(kv.substr(0, pos));
            val = trim(kv.substr(pos+1));
        } else {
            key = trim(kv);
        }

        const char* pkey = key.c_str();
        if (stricmp(pkey, "domain") == 0) {
            domain = val;
        }
        else if (stricmp(pkey, "path") == 0) {
            path = val;
        }
        else if (stricmp(pkey, "max-age") == 0) {
            max_age = atoi(val.c_str());
        }
        else if (stricmp(pkey, "secure") == 0) {
            secure = true;
        }
        else if (stricmp(pkey, "httponly") == 0) {
            httponly = true;
        }
        else if (val.size() > 0) {
            name = key;
            value = val;
        }
        else {
            hlogw("Unrecognized key '%s'", key.c_str());
        }
    }
    return !name.empty() && !value.empty();
}

std::string HttpCookie::dump() const {
    assert(!name.empty() && !value.empty());
    std::string res;
    res = name;
    res += "=";
    res += value;

    if (!domain.empty()) {
        res += "; domain=";
        res += domain;
    }

    if (!path.empty()) {
        res += "; path=";
        res += path;
    }

    if (max_age > 0) {
        res += "; max-age=";
        res += hv::to_string(max_age);
    }

    if (secure) {
        res += "; secure";
    }

    if (httponly) {
        res += "; httponly";
    }

    return res;
}

#ifndef WITHOUT_HTTP_CONTENT
// NOTE: json ignore number/string, 123/"123"

std::string HttpMessage::GetString(const char* key, const std::string& defvalue) {
    switch (content_type) {
    case APPLICATION_JSON:
    {
        if (!json.is_object()) {
            return defvalue;
        }
        const auto& value = json[key];
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
        if (!json.is_object()) {
            return defvalue;
        }
        const auto& value = json[key];
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
HV_EXPORT int HttpMessage::Get(const char* key, int defvalue) {
    return (int)Get<int64_t>(key, defvalue);
}

template<>
HV_EXPORT double HttpMessage::Get(const char* key, double defvalue) {
    if (content_type == APPLICATION_JSON) {
        if (!json.is_object()) {
            return defvalue;
        }
        const auto& value = json[key];
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
HV_EXPORT float HttpMessage::Get(const char* key, float defvalue) {
    return (float)Get<double>(key, defvalue);
}

template<>
HV_EXPORT bool HttpMessage::Get(const char* key, bool defvalue) {
    if (content_type == APPLICATION_JSON) {
        if (!json.is_object()) {
            return defvalue;
        }
        const auto& value = json[key];
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
    if (content_length == 0) {
        DumpBody();
        content_length = body.size();
    }
    if (iter == headers.end() && content_length != 0 && !IsChunked()) {
        headers["Content-Length"] = hv::to_string(content_length);
    }
}

bool HttpMessage::IsChunked() {
    auto iter = headers.find("Transfer-Encoding");
    return iter == headers.end() ? false : stricmp(iter->second.c_str(), "chunked") == 0;
}

bool HttpMessage::IsKeepAlive() {
    bool keepalive = true;
    auto iter = headers.find("connection");
    if (iter != headers.end()) {
        const char* keepalive_value = iter->second.c_str();
        if (stricmp(keepalive_value, "keep-alive") == 0) {
            keepalive = true;
        }
        else if (stricmp(keepalive_value, "close") == 0) {
            keepalive = false;
        }
        else if (stricmp(keepalive_value, "upgrade") == 0) {
            keepalive = true;
        }
    }
    else if (http_major == 1 && http_minor == 0) {
        keepalive = false;
    }
    return keepalive;
}

void HttpMessage::DumpHeaders(std::string& str) {
    FillContentType();
    FillContentLength();

    // headers
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

    // cookies
    const char* cookie_field = "Cookie";
    if (type == HTTP_RESPONSE) {
        cookie_field = "Set-Cookie";
    }
    for (auto& cookie : cookies) {
        str += cookie_field;
        str += ": ";
        str += cookie.dump();
        str += "\r\n";
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
        body = dump_json(json, 2);
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

void HttpMessage::DumpBody(std::string& str) {
    DumpBody();
    const char* content = (const char*)Content();
    int content_length = ContentLength();
    if (content && content_length) {
        str.append(content, content_length);
    }
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
        DumpBody(str);
    }
    return str;
}

void HttpRequest::DumpUrl() {
    if (url.size() != 0 && strstr(url.c_str(), "://") != NULL) {
        // have been complete url
        return;
    }
    std::string str;
    // scheme://
    str = scheme;
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
            str += Host();
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
    scheme = url.substr(parser.field_data[UF_SCHEMA].off, parser.field_data[UF_SCHEMA].len);
    // host
    if (parser.field_set & (1<<UF_HOST)) {
        host = url.substr(parser.field_data[UF_HOST].off, parser.field_data[UF_HOST].len);
    }
    // port
    port = parser.port ? parser.port : strcmp(scheme.c_str(), "https") ? DEFAULT_HTTP_PORT : DEFAULT_HTTPS_PORT;
    // path
    if (parser.field_set & (1<<UF_PATH)) {
        const char* sp = url.c_str() + parser.field_data[UF_PATH].off;
        char* ep = (char*)(sp + parser.field_data[UF_PATH].len);
        char ev = *ep;
        *ep = '\0';
        path = url_unescape(sp);
        if (ev != '\0') {
            *ep = ev;
            path += ep;
        }
    }
    // query
    if (parser.field_set & (1<<UF_QUERY)) {
        parse_query_params(url.c_str()+parser.field_data[UF_QUERY].off, query_params);
    }
}

std::string HttpRequest::Dump(bool is_dump_headers, bool is_dump_body) {
    ParseUrl();

    std::string str;
    str.reserve(MAX(512, path.size() + 128));
    // GET / HTTP/1.1\r\n
    str = asprintf("%s %s HTTP/%d.%d\r\n",
            http_method_str(method), path.c_str(),
            (int)http_major, (int)http_minor);
    if (is_dump_headers) {
        // Host:
        if (headers.find("Host") == headers.end()) {
            if (port == 0 ||
                port == DEFAULT_HTTP_PORT ||
                port == DEFAULT_HTTPS_PORT) {
                headers["Host"] = host;
            }
            else {
                headers["Host"] = asprintf("%s:%d", host.c_str(), port);
            }
        }
        DumpHeaders(str);
    }
    str += "\r\n";
    if (is_dump_body) {
        DumpBody(str);
    }
    return str;
}

std::string HttpResponse::Dump(bool is_dump_headers, bool is_dump_body) {
    char c_str[256] = {0};
    std::string str;
    str.reserve(512);
    // HTTP/1.1 200 OK\r\n
    snprintf(c_str, sizeof(c_str), "HTTP/%d.%d %d %s\r\n",
            (int)http_major, (int)http_minor,
            (int)status_code, http_status_str(status_code));
    str = c_str;
    if (is_dump_headers) {
        if (*s_date) {
            headers["Date"] = s_date;
        } else {
            headers["Date"] = gmtime_fmt(time(NULL), c_str);
        }
        DumpHeaders(str);
    }
    str += "\r\n";
    if (is_dump_body) {
        DumpBody(str);
    }
    return str;
}
