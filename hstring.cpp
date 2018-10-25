#include "hstring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <iostream>
#include <sstream>

#define SPACE_CHARS     " \t\r\n"

int vscprintf(const char* fmt, va_list ap) {
    return vsnprintf(NULL, 0, fmt, ap);
}

string asprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vscprintf(fmt, ap) + 1;
    va_end(ap);
    // must recall va_start in linux
    va_start(ap, fmt);
    char* buf = (char*)malloc(len);
    memset(buf, 0, len);
    vsnprintf(buf, len, fmt, ap);
    va_end(ap);

    string res(buf);
    free(buf);
    return res;
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

string trim(const string& str) {
    string::size_type pos1 = str.find_first_not_of(SPACE_CHARS);
    if (pos1 == string::npos)   return "";

    string::size_type pos2 = str.find_last_not_of(SPACE_CHARS);
    return str.substr(pos1, pos2-pos1+1);
}

string trimL(const string& str) {
    string::size_type pos = str.find_first_not_of(SPACE_CHARS);
    if (pos == string::npos)    return "";
    return str.substr(pos);
}

string trimR(const string& str) {
    string::size_type pos = str.find_last_not_of(SPACE_CHARS);
    return str.substr(0, pos+1);
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
