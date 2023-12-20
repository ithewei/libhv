#ifndef HV_URL_H_
#define HV_URL_H_

#include <string> // import std::string

#include "hexport.h"

class HV_EXPORT HUrl {
public:
    static std::string escape(const std::string& str, const char* unescaped_chars = "");
    static std::string unescape(const std::string& str);

    HUrl() : port(0) {}
    ~HUrl() {}

    void reset();
    bool parse(const std::string& url);
    const std::string& dump();

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

namespace hv {

HV_INLINE std::string escapeURL(const std::string& url) {
    return HUrl::escape(url, ":/@?=&#+");
}

HV_EXPORT std::string escapeHTML(const std::string& str);

} // end namespace hv

#endif // HV_URL_H_
