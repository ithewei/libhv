#include "hurl.h"

#include "hdef.h"
#include "hbase.h"

/*
static bool Curl_isunreserved(unsigned char in)
{
    switch(in) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j':
    case 'k': case 'l': case 'm': case 'n': case 'o':
    case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E':
    case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '-': case '.': case '_': case '~':
      return TRUE;
    default:
      break;
    }
    return FLASE;
}
*/

static inline bool is_unambiguous(char c) {
    return IS_ALPHANUM(c) ||
           c == '-' ||
           c == '_' ||
           c == '.' ||
           c == '~';
}

static inline bool char_in_str(char c, const char* str) {
    const char* p = str;
    while (*p && *p != c) ++p;
    return *p != '\0';
}

static inline unsigned char hex2i(char hex) {
    return hex <= '9' ? hex - '0' :
        hex <= 'F' ? hex - 'A' + 10 : hex - 'a' + 10;
}

std::string HUrl::escape(const std::string& str, const char* unescaped_chars) {
    std::string ostr;
    static char tab[] = "0123456789ABCDEF";
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str.c_str());
    char szHex[4] = "%00";
    while (*p != '\0') {
        if (is_unambiguous(*p) || char_in_str(*p, unescaped_chars)) {
            ostr += *p;
        }
        else {
            szHex[1] = tab[*p >> 4];
            szHex[2] = tab[*p & 0xF];
            ostr += szHex;
        }
        ++p;
    }
    return ostr;
}

std::string HUrl::unescape(const std::string& str) {
    std::string ostr;
    const char* p = str.c_str();
    while (*p != '\0') {
        if (*p == '%' &&
            IS_HEX(p[1]) &&
            IS_HEX(p[2])) {
            ostr += ((hex2i(p[1]) << 4) | hex2i(p[2]));
            p += 3;
        }
        else {
            if (*p == '+') {
                ostr += ' ';
            } else {
                ostr += *p;
            }
            ++p;
        }
    }
    return ostr;
}

bool HUrl::parse(const std::string& url) {
    reset();
    this->url = url;
    hurl_t stURL;
    if (hv_parse_url(&stURL, url.c_str()) != 0) {
        return false;
    }
    int len = stURL.fields[HV_URL_SCHEME].len;
    if (len > 0) {
        scheme = url.substr(stURL.fields[HV_URL_SCHEME].off, len);
    }
    len = stURL.fields[HV_URL_USERNAME].len;
    if (len > 0) {
        username = url.substr(stURL.fields[HV_URL_USERNAME].off, len);
        len = stURL.fields[HV_URL_PASSWORD].len;
        if (len > 0) {
            password = url.substr(stURL.fields[HV_URL_PASSWORD].off, len);
        }
    }
    len = stURL.fields[HV_URL_HOST].len;
    if (len > 0) {
        host = url.substr(stURL.fields[HV_URL_HOST].off, len);
    }
    port = stURL.port;
    len = stURL.fields[HV_URL_PATH].len;
    if (len > 0) {
        path = url.substr(stURL.fields[HV_URL_PATH].off, len);
    } else {
        path = "/";
    }
    len = stURL.fields[HV_URL_QUERY].len;
    if (len > 0) {
        query = url.substr(stURL.fields[HV_URL_QUERY].off, len);
    }
    len = stURL.fields[HV_URL_FRAGMENT].len;
    if (len > 0) {
        fragment = url.substr(stURL.fields[HV_URL_FRAGMENT].off, len);
    }
    return true;
}

const std::string& HUrl::dump() {
    url.clear();
    // scheme://
    if (!scheme.empty()) {
        url += scheme;
        url += "://";
    }
    // user:pswd@
    if (!username.empty()) {
        url += username;
        if (!password.empty()) {
            url += ":";
            url += password;
        }
        url += "@";
    }
    // host:port
    if (!host.empty()) {
        url += host;
        if (port != 80 && port != 443) {
            char buf[16] = {0};
            snprintf(buf, sizeof(buf), ":%d", port);
            url += port;
        }
    }
    // /path
    if (!path.empty()) {
        url += path;
    }
    // ?query
    if (!query.empty()) {
        url += '?';
        url += query;
    }
    // #fragment
    if (!fragment.empty()) {
        url += '#';
        url += fragment;
    }
    return url;
}
