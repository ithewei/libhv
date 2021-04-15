#include "hurl.h"

#include "hdef.h"

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

static inline unsigned char hex2i(char hex) {
    return hex <= '9' ? hex - '0' :
        hex <= 'F' ? hex - 'A' + 10 : hex - 'a' + 10;
}

std::string url_escape(const char* istr) {
    std::string ostr;
    const char* p = istr;
    char szHex[4] = {0};
    while (*p != '\0') {
        if (is_unambiguous(*p)) {
            ostr += *p;
        }
        else {
            sprintf(szHex, "%%%02X", *p);
            ostr += szHex;
        }
        ++p;
    }
    return ostr;
}

std::string url_unescape(const char* istr) {
    std::string ostr;
    const char* p = istr;
    while (*p != '\0') {
        if (*p == '%' &&
            IS_HEX(p[1]) &&
            IS_HEX(p[2])) {
            ostr += ((hex2i(p[1]) << 4) | hex2i(p[2]));
            p += 3;
        }
        else {
            ostr += *p;
            ++p;
        }
    }
    return ostr;
}
