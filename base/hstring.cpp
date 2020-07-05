#include "hstring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static inline int vscprintf(const char* fmt, va_list ap) {
    return vsnprintf(NULL, 0, fmt, ap);
}

string asprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vscprintf(fmt, ap);
    va_end(ap);

    string str;
    str.reserve(len+1);
    // must resize to set str.size
    str.resize(len);
    // must recall va_start on unix
    va_start(ap, fmt);
    vsnprintf((char*)str.data(), len+1, fmt, ap);
    va_end(ap);

    return str;
}

StringList split(const string& str, char delim) {
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

hv::KeyValue splitKV(const string& str, char kv_kv, char k_v) {
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

string trim(const string& str, const char* chars) {
    string::size_type pos1 = str.find_first_not_of(chars);
    if (pos1 == string::npos)   return "";

    string::size_type pos2 = str.find_last_not_of(chars);
    return str.substr(pos1, pos2-pos1+1);
}

string trimL(const string& str, const char* chars) {
    string::size_type pos = str.find_first_not_of(chars);
    if (pos == string::npos)    return "";
    return str.substr(pos);
}

string trimR(const string& str, const char* chars) {
    string::size_type pos = str.find_last_not_of(chars);
    return str.substr(0, pos+1);
}

string trim_pairs(const string& str, const char* pairs) {
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

string replace(const string& str, const string& find, const string& rep) {
    string::size_type pos = 0;
    string::size_type a = find.size();
    string::size_type b = rep.size();

    string res(str);
    while ((pos = res.find(find, pos)) != string::npos) {
        res.replace(pos, a, rep);
        pos += b;
    }
    return res;
}

string basename(const string& str) {
    string::size_type pos1 = str.find_last_not_of("/\\");
    if (pos1 == string::npos) {
        return "/";
    }
    string::size_type pos2 = str.find_last_of("/\\", pos1);
    if (pos2 == string::npos) {
        pos2 = 0;
    } else {
        pos2++;
    }

    return str.substr(pos2, pos1-pos2+1);
}

string dirname(const string& str) {
    string::size_type pos1 = str.find_last_not_of("/\\");
    if (pos1 == string::npos) {
        return "/";
    }
    string::size_type pos2 = str.find_last_of("/\\", pos1);
    if (pos2 == string::npos) {
        return ".";
    } else if (pos2 == 0) {
        pos2 = 1;
    }

    return str.substr(0, pos2);
}

string filename(const string& str) {
    string::size_type pos1 = str.find_last_of("/\\");
    if (pos1 == string::npos) {
        pos1 = 0;
    } else {
        pos1++;
    }
    string file = str.substr(pos1, -1);

    string::size_type pos2 = file.find_last_of(".");
    if (pos2 == string::npos) {
        return file;
    }
    return file.substr(0, pos2);
}

string suffixname(const string& str) {
    string::size_type pos1 = str.find_last_of("/\\");
    if (pos1 == string::npos) {
        pos1 = 0;
    } else {
        pos1++;
    }
    string file = str.substr(pos1, -1);

    string::size_type pos2 = file.find_last_of(".");
    if (pos2 == string::npos) {
        return "";
    }
    return file.substr(pos2+1, -1);
}
