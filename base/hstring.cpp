#include "hstring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <iostream>
#include <sstream>

char* strupper(char* str) {
    char* p = str;
    while (*p != '\0') {
        if (*p >= 'a' && *p <= 'z') {
            *p &= ~0x20;
        }
        ++p;
    }
    return str;
}

char* strlower(char* str) {
    char* p = str;
    while (*p != '\0') {
        if (*p >= 'A' && *p <= 'Z') {
            *p |= 0x20;
        }
        ++p;
    }
    return str;
}

char* strreverse(char* str) {
    if (str == NULL) return NULL;
    char* b = str;
    char* e = str;
    while(*e) {++e;}
    --e;
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
    std::stringstream ss;
    ss << str;
    string item;
    StringList res;
    while (std::getline(ss, item, delim)) {
        res.push_back(item);
    }
    return res;
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
