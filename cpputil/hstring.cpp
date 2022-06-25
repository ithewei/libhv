#include "hstring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

namespace hv {

std::string                         empty_string;
std::map<std::string, std::string>  empty_map;

std::string& toupper(std::string& str) {
    // std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    char* p = (char*)str.c_str();
    while (*p != '\0') {
        if (*p >= 'a' && *p <= 'z') {
            *p &= ~0x20;
        }
        ++p;
    }
    return str;
}

std::string& tolower(std::string& str) {
    // std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    char* p = (char*)str.c_str();
    while (*p != '\0') {
        if (*p >= 'A' && *p <= 'Z') {
            *p |= 0x20;
        }
        ++p;
    }
    return str;
}

std::string& reverse(std::string& str) {
    // std::reverse(str.begin(), str.end());
    char* b = (char*)str.c_str();
    char* e = b + str.length() - 1;
    char tmp;
    while (e > b) {
        tmp = *e;
        *e = *b;
        *b = tmp;
        --e;
        ++b;
    }
    return str;
}

bool startswith(const std::string& str, const std::string& start) {
    if (str.length() < start.length()) return false;
    return str.compare(0, start.length(), start) == 0;
}

bool endswith(const std::string& str, const std::string& end) {
    if (str.length() < end.length()) return false;
    return str.compare(str.length() - end.length(), end.length(), end) == 0;
}

bool contains(const std::string& str, const std::string& sub) {
    if (str.length() < sub.length()) return false;
    return str.find(sub) != std::string::npos;
}

static inline int vscprintf(const char* fmt, va_list ap) {
    return vsnprintf(NULL, 0, fmt, ap);
}

std::string asprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vscprintf(fmt, ap);
    va_end(ap);

    std::string str;
    str.reserve(len+1);
    // must resize to set str.size
    str.resize(len);
    // must recall va_start on unix
    va_start(ap, fmt);
    vsnprintf((char*)str.data(), len+1, fmt, ap);
    va_end(ap);

    return str;
}

StringList split(const std::string& str, char delim) {
    /*
    std::stringstream ss;
    ss << str;
    string item;
    StringList res;
    while (std::getline(ss, item, delim)) {
        res.push_back(item);
    }
    return res;
    */
    const char* p = str.c_str();
    const char* value = p;
    StringList res;
    while (*p != '\0') {
        if (*p == delim) {
            res.push_back(std::string(value, p-value));
            value = p+1;
        }
        ++p;
    }
    res.push_back(value);
    return res;
}

hv::KeyValue splitKV(const std::string& str, char kv_kv, char k_v) {
    enum {
        s_key,
        s_value,
    } state = s_key;
    const char* p = str.c_str();
    const char* key = p;
    const char* value = NULL;
    int key_len = 0;
    int value_len = 0;
    hv::KeyValue kvs;
    while (*p != '\0') {
        if (*p == kv_kv) {
            if (key_len && value_len) {
                kvs[std::string(key, key_len)] = std::string(value, value_len);
                key_len = value_len = 0;
            }
            state = s_key;
            key = p+1;
        }
        else if (*p == k_v) {
            state = s_value;
            value = p+1;
        }
        else {
            state == s_key ? ++key_len : ++value_len;
        }
        ++p;
    }
    if (key_len && value_len) {
        kvs[std::string(key, key_len)] = std::string(value, value_len);
    }
    return kvs;
}

std::string trim(const std::string& str, const char* chars) {
    std::string::size_type pos1 = str.find_first_not_of(chars);
    if (pos1 == std::string::npos)   return "";

    std::string::size_type pos2 = str.find_last_not_of(chars);
    return str.substr(pos1, pos2-pos1+1);
}

std::string ltrim(const std::string& str, const char* chars) {
    std::string::size_type pos = str.find_first_not_of(chars);
    if (pos == std::string::npos)    return "";
    return str.substr(pos);
}

std::string rtrim(const std::string& str, const char* chars) {
    std::string::size_type pos = str.find_last_not_of(chars);
    return str.substr(0, pos+1);
}

std::string trim_pairs(const std::string& str, const char* pairs) {
    const char* s = str.c_str();
    const char* e = str.c_str() + str.size() - 1;
    const char* p = pairs;
    bool is_pair = false;
    while (*p != '\0' && *(p+1) != '\0') {
        if (*s == *p && *e == *(p+1)) {
            is_pair = true;
            break;
        }
        p += 2;
    }
    return is_pair ? str.substr(1, str.size()-2) : str;
}

std::string replace(const std::string& str, const std::string& find, const std::string& rep) {
    std::string res(str);
    std::string::size_type pos = res.find(find);
    if (pos != std::string::npos) {
        res.replace(pos, find.size(), rep);
    }
    return res;
}

std::string replaceAll(const std::string& str, const std::string& find, const std::string& rep) {
    std::string::size_type pos = 0;
    std::string::size_type a = find.size();
    std::string::size_type b = rep.size();

    std::string res(str);
    while ((pos = res.find(find, pos)) != std::string::npos) {
        res.replace(pos, a, rep);
        pos += b;
    }
    return res;
}

} // end namespace hv
