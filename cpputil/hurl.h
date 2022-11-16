#ifndef HV_URL_H_
#define HV_URL_H_

#include <string> // import std::string

#include "hexport.h"

class HV_EXPORT HUrl {
public:
    static std::string escape(const std::string& str, const char* unescaped_chars = "");
    static std::string unescape(const std::string& str);
    static inline std::string escapeUrl(const std::string& url) {
        return escape(url, ":/@?=&#+");
    }

    HUrl() : port(0) {}
    ~HUrl() {}

    bool parse(const std::string& url);
    const std::string& dump();
    void reset() {
        url.clear();
        scheme.clear();
        username.clear();
        password.clear();
        host.clear();
        port = 0;
        path.clear();
        query.clear();
        fragment.clear();
    }

    std::string url;
    std::string scheme;
    std::string username;
    std::string password;
    std::string host;
    int         port;
    std::string path;
    std::string query;
    std::string fragment;
};

#endif // HV_URL_H_
