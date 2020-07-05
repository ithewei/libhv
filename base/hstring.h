#ifndef HV_STRING_H_
#define HV_STRING_H_

#include <string>
#include <vector>
#include <sstream>

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
static inline std::string to_string(const T& t) {
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

template<typename T>
static inline T from_string(const std::string& str) {
    T t;
    std::istringstream iss(str);
    iss >> t;
    return t;
}
}

#define SPACE_CHARS     " \t\r\n"
#define PAIR_CHARS      "{}[]()<>\"\"\'\'``"

string asprintf(const char* fmt, ...);
// x,y,z
StringList split(const string& str, char delim = ',');
// user=amdin&pswd=123456
hv::KeyValue splitKV(const string& str, char kv_kv = '&', char k_v = '=');
string trim(const string& str, const char* chars = SPACE_CHARS);
string trimL(const string& str, const char* chars = SPACE_CHARS);
string trimR(const string& str, const char* chars = SPACE_CHARS);
string trim_pairs(const string& str, const char* pairs = PAIR_CHARS);
string replace(const string& str, const string& find, const string& rep);

// str=/mnt/share/image/test.jpg
// basename=test.jpg
// dirname=/mnt/share/image
// filename=test
// suffixname=jpg
string basename(const string& str);
string dirname(const string& str);
string filename(const string& str);
string suffixname(const string& str);

#endif // HV_STRING_H_
