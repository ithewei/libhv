#include "HttpMessage.h"

#include <string.h>

#include "htime.h"
#include "hlog.h"
#include "hurl.h"

using namespace hv;

http_headers DefaultHeaders;
http_body    NoBody;
HttpCookie   NoCookie;
char HttpMessage::s_date[32] = {0};

bool HttpCookie::parse(const std::string& str) {
    std::stringstream ss;
    ss << str;
    std::string line;
    std::string::size_type pos;
    std::string key;
    std::string val;

    reset();
    while (std::getline(ss, line, ';')) {
        pos = line.find_first_of('=');
        if (pos != std::string::npos) {
            key = trim(line.substr(0, pos));
            val = trim(line.substr(pos+1));
            const char* pkey = key.c_str();
            if (stricmp(pkey, "Domain") == 0) {
                domain = val;
            }
            else if (stricmp(pkey, "Path") == 0) {
                path = val;
            }
            else if (stricmp(pkey, "Expires") == 0) {
                expires = val;
            }
            else if (stricmp(pkey, "Max-Age") == 0) {
                max_age = atoi(val.c_str());
            }
            else if (stricmp(pkey, "SameSite") == 0) {
                samesite =  stricmp(val.c_str(), "Strict") == 0 ? HttpCookie::SameSite::Strict :
                            stricmp(val.c_str(), "Lax")    == 0 ? HttpCookie::SameSite::Lax    :
                            stricmp(val.c_str(), "None")   == 0 ? HttpCookie::SameSite::None   :
                                                                  HttpCookie::SameSite::Default;
            }
            else if (stricmp(pkey, "Priority") == 0) {
                priority =  stricmp(val.c_str(), "Low")    == 0 ? HttpCookie::Priority::Low    :
                            stricmp(val.c_str(), "Medium") == 0 ? HttpCookie::Priority::Medium :
                            stricmp(val.c_str(), "High")   == 0 ? HttpCookie::Priority::High   :
                                                                  HttpCookie::Priority::NotSet ;
            }
            else {
                if (name.empty()) {
                    name = key;
                    value = val;
                }
                kv[key] = val;
            }
        } else {
            key = trim(line);
            const char* pkey = key.c_str();
            if (stricmp(pkey, "Secure") == 0) {
                secure = true;
            }
            else if (stricmp(pkey, "HttpOnly") == 0) {
                httponly = true;
            }
            else {
                hlogw("Unrecognized key '%s'", key.c_str());
            }
        }

    }
    return !name.empty();
}

std::string HttpCookie::dump() const {
    assert(!name.empty() || !kv.empty());
    std::string res;

    if (!name.empty()) {
        res = name;
        res += "=";
        res += value;
    }

    for (auto& pair : kv) {
        if (pair.first == name) continue;
        if (!res.empty()) res += "; ";
        res += pair.first;
        res += "=";
        res += pair.second;
    }

    if (!domain.empty()) {
        res += "; Domain=";
        res += domain;
    }

    if (!path.empty()) {
        res += "; Path=";
        res += path;
    }

    if (max_age > 0) {
        res += "; Max-Age=";
        res += hv::to_string(max_age);
    } else if (!expires.empty()) {
        res += "; Expires=";
        res += expires;
    }

    if (samesite != HttpCookie::SameSite::Default) {
        res += "; SameSite=";
        res += samesite == HttpCookie::SameSite::Strict ? "Strict" :
               samesite == HttpCookie::SameSite::Lax    ? "Lax"    :
                                                          "None"   ;
    }

    if (priority != HttpCookie::Priority::NotSet) {
        res += "; Priority=";
        res += priority == HttpCookie::Priority::Low    ? "Low"    :
               priority == HttpCookie::Priority::Medium ? "Medium" :
                                                          "High"   ;
    }

    if (secure) {
        res += "; Secure";
    }

    if (httponly) {
        res += "; HttpOnly";
    }

    return res;
}

#ifndef WITHOUT_HTTP_CONTENT
// NOTE: json ignore number/string, 123/"123"

std::string HttpMessage::GetString(const char* key, const std::string& defvalue) {
    switch (ContentType()) {
    case APPLICATION_JSON:
    {
        if (json.empty()) {
            ParseBody();
        }
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
        if (form.empty()) {
            ParseBody();
        }
        auto iter = form.find(key);
        if (iter != form.end()) {
            return iter->second.content;
        }
    }
        break;
    case APPLICATION_URLENCODED:
    {
        if (kv.empty()) {
            ParseBody();
        }
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
    if (ContentType() == APPLICATION_JSON) {
        if (json.empty()) {
            ParseBody();
        }
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
    if (ContentType() == APPLICATION_JSON) {
        if (json.empty()) {
            ParseBody();
        }
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
    if (ContentType() == APPLICATION_JSON) {
        if (json.empty()) {
            ParseBody();
        }
        if (!json.is_object()) {
            return defvalue;
        }
        const auto& value = json[key];
        if (value.is_boolean()) {
            return value;
        }
        else if (value.is_string()) {
            std::string str = value;
            return hv_getboolean(str.c_str());
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
        return str.empty() ? defvalue : hv_getboolean(str.c_str());
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
        content_length = atoll(iter->second.c_str());
    }
    if (content_length == 0) {
        DumpBody();
        content_length = body.size();
    }
    if (iter == headers.end() && !IsChunked() && content_type != TEXT_EVENT_STREAM) {
        if (content_length != 0 || type == HTTP_RESPONSE) {
            headers["Content-Length"] = hv::to_string(content_length);
        }
    }
}

bool HttpMessage::IsChunked() {
    auto iter = headers.find("Transfer-Encoding");
    return iter != headers.end() && stricmp(iter->second.c_str(), "chunked") == 0;
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
    size_t content_length = ContentLength();
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
            return -1;
        }
        const char* boundary = strstr(iter->second.c_str(), "boundary=");
        if (boundary == NULL) {
            return -1;
        }
        boundary += strlen("boundary=");
        std::string strBoundary(boundary);
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
    std::string str;
    if (url.size() != 0 &&
        *url.c_str() != '/' &&
        strstr(url.c_str(), "://") != NULL) {
        // have been complete url
        goto query;
    }
    // scheme://
    str = scheme;
    str += "://";
    // host:port
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
            str += hv::asprintf("%s:%d", host.c_str(), port);
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
    url = str;
query:
    // ?query
    if (strchr(url.c_str(), '?') == NULL &&
        query_params.size() != 0) {
        url += '?';
        url += dump_query_params(query_params);
    }
}

void HttpRequest::ParseUrl() {
    DumpUrl();
    hurl_t parser;
    hv_parse_url(&parser, url.c_str());
    // scheme
    std::string scheme_ = url.substr(parser.fields[HV_URL_SCHEME].off, parser.fields[HV_URL_SCHEME].len);
    // host
    std::string host_(host);
    if (parser.fields[HV_URL_HOST].len > 0) {
        host_ = url.substr(parser.fields[HV_URL_HOST].off, parser.fields[HV_URL_HOST].len);
    }
    // port
    int port_ = parser.port ? parser.port : strcmp(scheme_.c_str(), "https") ? DEFAULT_HTTP_PORT : DEFAULT_HTTPS_PORT;
    if (!proxy) {
        scheme = scheme_;
        host = host_;
        port = port_;
    }
    FillHost(host_.c_str(), port_);
    // path
    if (parser.fields[HV_URL_PATH].len > 0) {
        path = url.substr(parser.fields[HV_URL_PATH].off);
    }
    // query
    if (parser.fields[HV_URL_QUERY].len > 0) {
        parse_query_params(url.c_str()+parser.fields[HV_URL_QUERY].off, query_params);
    }
}

std::string HttpRequest::Path() {
    const char* s = path.c_str();
    const char* e = s;
    while (*e && *e != '?' && *e != '#') ++e;
    return HUrl::unescape(std::string(s, e));
}

void HttpRequest::FillHost(const char* host, int port) {
    if (headers.find("Host") == headers.end()) {
        if (port == 0 ||
            port == DEFAULT_HTTP_PORT ||
            port == DEFAULT_HTTPS_PORT) {
            headers["Host"] = host;
        } else {
            headers["Host"] = asprintf("%s:%d", host, port);
        }
    }
}

void HttpRequest::SetHost(const char* host, int port) {
    this->host = host;
    this->port = port;
    FillHost(host, port);
}

void HttpRequest::SetProxy(const char* host, int port) {
    this->scheme = "http";
    this->host = host;
    this->port = port;
    proxy = 1;
}

std::string HttpRequest::Dump(bool is_dump_headers, bool is_dump_body) {
    ParseUrl();

    std::string str;
    str.reserve(MAX(512, path.size() + 128));
    // GET / HTTP/1.1\r\n
    str = asprintf("%s %s HTTP/%d.%d\r\n",
            http_method_str(method),
            proxy ? url.c_str() : path.c_str(),
            (int)http_major, (int)http_minor);
    if (is_dump_headers) {
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
