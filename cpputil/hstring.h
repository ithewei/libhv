#ifndef HV_STRING_H_
#define HV_STRING_H_

#include <string>
#include <vector>

#include <iostream>
#include <sstream>

#include "hexport.h"
#include "hplatform.h"
#include "hmap.h"

#define SPACE_CHARS     " \t\r\n"
#define PAIR_CHARS      "{}[]()<>\"\"\'\'``"

namespace hv {

HV_EXPORT extern std::string                        empty_string;
HV_EXPORT extern std::map<std::string, std::string> empty_map;

typedef std::vector<std::string> StringList;

// std::map<std::string, std::string, StringCaseLess>
class StringCaseLess : public std::less<std::string> {
public:
    bool operator()(const std::string& lhs, const std::string& rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};

// NOTE: low-version NDK not provide std::to_string
template<typename T>
HV_INLINE std::string to_string(const T& t) {
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

template<typename T>
HV_INLINE T from_string(const std::string& str) {
    T t;
    std::istringstream iss(str);
    iss >> t;
    return t;
}

template<typename T>
HV_INLINE void print(const T& t) {
    std::cout << t;
}

template<typename T>
HV_INLINE void println(const T& t) {
    std::cout << t << std::endl;
}

HV_EXPORT std::string& toupper(std::string& str);
HV_EXPORT std::string& tolower(std::string& str);
HV_EXPORT std::string& reverse(std::string& str);

HV_EXPORT bool startswith(const std::string& str, const std::string& start);
HV_EXPORT bool endswith(const std::string& str, const std::string& end);
HV_EXPORT bool contains(const std::string& str, const std::string& sub);

HV_EXPORT std::string asprintf(const char* fmt, ...);
// x,y,z
HV_EXPORT StringList split(const std::string& str, char delim = ',');
// k1=v1&k2=v2
HV_EXPORT hv::KeyValue splitKV(const std::string& str, char kv_kv = '&', char k_v = '=');
HV_EXPORT std::string trim(const std::string& str, const char* chars = SPACE_CHARS);
HV_EXPORT std::string ltrim(const std::string& str, const char* chars = SPACE_CHARS);
HV_EXPORT std::string rtrim(const std::string& str, const char* chars = SPACE_CHARS);
HV_EXPORT std::string trim_pairs(const std::string& str, const char* pairs = PAIR_CHARS);
HV_EXPORT std::string replace(const std::string& str, const std::string& find, const std::string& rep);
HV_EXPORT std::string replaceAll(const std::string& str, const std::string& find, const std::string& rep);

struct HV_EXPORT NetAddr {
    std::string     ip;
    int             port;

    NetAddr() : port(0) {}
    NetAddr(const std::string& _ip, int _port) : ip(_ip), port(_port) {}
    NetAddr(const std::string& ipport) { from_string(ipport); }

    void from_string(const std::string& ipport);
    std::string to_string();
    static std::string to_string(const char* ip, int port);
};

// windows wchar and utf8/ansi conver
#ifdef OS_WIN
HV_EXPORT std::string wchar_to_string(const UINT codePage, const std::wstring &wstr);
HV_EXPORT std::wstring string_to_wchar(const UINT codePage, const std::string &str);

HV_INLINE std::string wchar_to_utf8(const std::wstring &wstr) {
    return wchar_to_string(CP_UTF8, wstr);
}

HV_INLINE std::string wchar_to_ansi(const std::wstring &wstr) {
    return wchar_to_string(CP_ACP, wstr);
}

HV_INLINE std::wstring utf8_to_wchar(const std::string &str) {
    return string_to_wchar(CP_UTF8, str);
}

HV_INLINE std::string utf8_to_ansi(const std::string &str) {
    return wchar_to_string(CP_ACP, string_to_wchar(CP_UTF8, str));
}

HV_INLINE std::string ansi_to_utf8(const std::string &str) {
    return wchar_to_string(CP_UTF8, string_to_wchar(CP_ACP, str));
}

#endif // OS_WIN

} // end namespace hv

#endif // HV_STRING_H_
