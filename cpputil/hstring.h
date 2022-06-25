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

} // end namespace hv

#endif // HV_STRING_H_
