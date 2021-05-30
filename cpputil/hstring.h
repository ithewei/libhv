#ifndef HV_STRING_H_
#define HV_STRING_H_

#include <string>
#include <vector>
#include <sstream>

#include "hexport.h"
#include "hbase.h"
#include "hmap.h"

using std::string;
typedef std::vector<string> StringList;

// std::map<std::string, std::string, StringCaseLess>
class StringCaseLess : public std::less<std::string> {
public:
    bool operator()(const std::string& lhs, const std::string& rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};

namespace hv {
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
}

#define SPACE_CHARS     " \t\r\n"
#define PAIR_CHARS      "{}[]()<>\"\"\'\'``"

HV_EXPORT string asprintf(const char* fmt, ...);
// x,y,z
HV_EXPORT StringList split(const string& str, char delim = ',');
// user=amdin&pswd=123456
HV_EXPORT hv::KeyValue splitKV(const string& str, char kv_kv = '&', char k_v = '=');
HV_EXPORT string trim(const string& str, const char* chars = SPACE_CHARS);
HV_EXPORT string trimL(const string& str, const char* chars = SPACE_CHARS);
HV_EXPORT string trimR(const string& str, const char* chars = SPACE_CHARS);
HV_EXPORT string trim_pairs(const string& str, const char* pairs = PAIR_CHARS);
HV_EXPORT string replace(const string& str, const string& find, const string& rep);

// str=/mnt/share/image/test.jpg
// basename=test.jpg
// dirname=/mnt/share/image
// filename=test
// suffixname=jpg
HV_EXPORT string basename(const string& str);
HV_EXPORT string dirname(const string& str);
HV_EXPORT string filename(const string& str);
HV_EXPORT string suffixname(const string& str);

#endif // HV_STRING_H_
