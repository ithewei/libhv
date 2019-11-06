#include "http_content.h"

#include <sys/types.h>
#include <sys/stat.h>

#include "hdef.h"
#include "hstring.h"

#include "httpdef.h" // for http_content_type_str_by_suffix

static char hex2i(char hex) {
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    }
    switch (hex) {
        case 'A': case 'a': return 10;
        case 'B': case 'b': return 11;
        case 'C': case 'c': return 12;
        case 'D': case 'd': return 13;
        case 'E': case 'e': return 14;
        case 'F': case 'f': return 15;
        default: break;
    }
    return 0;
}

/*
bool Curl_isunreserved(unsigned char in)
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

// scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
static std::string escape(const std::string& param) {
    std::string str;
    const char* p = param.c_str();
    char escape[4] = {0};
    while (*p != '\0') {
        if (is_unambiguous(*p)) {
            str += *p;
        }
        else {
            sprintf(escape, "%%%02X", *p);
            str += escape;
        }
        ++p;
    }
    return str;
}

static std::string unescape(const char* escape_param) {
    std::string str;
    const char* p = escape_param;
    while (*p != '\0') {
        if (*p == '%' &&
            IS_HEX(p[1]) &&
            IS_HEX(p[2])) {
            str += (hex2i(p[1]) << 4 | hex2i(p[2]));
            p += 3;
        }
        else {
            str += *p;
            ++p;
        }
    }
    return str;
}

std::string dump_query_params(QueryParams& query_params) {
    std::string query_string;
    for (auto& pair : query_params) {
        if (query_string.size() != 0) {
            query_string += '&';
        }
        query_string += escape(pair.first);
        query_string += '=';
        query_string += escape(pair.second);
    }
    return query_string;
}

int parse_query_params(const char* query_string, QueryParams& query_params) {
    printf("%s\n", query_string);
    const char* p = strchr(query_string, '?');
    p = p ? p+1 : query_string;
    std::string unescape_string = unescape(p);
    p = unescape_string.c_str();

    enum {
        s_key,
        s_value,
    } state = s_key;

    const char* key = p;
    const char* value = NULL;
    int key_len = 0;
    int value_len = 0;
    while (*p != '\0') {
        if (*p == '&') {
            if (key_len && value_len) {
                query_params[std::string(key,key_len)] = std::string(value,value_len);
                key_len = value_len = 0;
            }
            state = s_key;
            key = p+1;
            printf("key=%s %p\n", key, key);
        }
        else if (*p == '=') {
            state = s_value;
            value = p+1;
            printf("value=%s %p\n", value, value);
        }
        else {
            state == s_key ? ++key_len : ++value_len;
        }
        ++p;
    }
    if (key_len && value_len) {
        query_params[std::string(key,key_len)] = std::string(value,value_len);
        key_len = value_len = 0;
    }
    return query_params.size() == 0 ? -1 : 0;
}

#ifndef WITHOUT_HTTP_CONTENT
std::string dump_json(Json& json) {
    return json.dump();
}

std::string g_parse_json_errmsg;
int parse_json(const char* str, Json& json, std::string& errmsg) {
    try {
        json = Json::parse(str);
    }
    catch(nlohmann::detail::exception e) {
        errmsg = e.what();
        return -1;
    }
    return (json.is_discarded() || json.is_null()) ? -1 : 0;
}

std::string dump_multipart(MultiPart& mp, const char* boundary) {
    char c_str[256] = {0};
    std::string str;
    for (auto& pair : mp) {
        str += "--";
        str += boundary;
        str += "\r\n";
        str += "Content-Disposition: form-data";
        snprintf(c_str, sizeof(c_str), "; name=\"%s\"", pair.first.c_str());
        str += c_str;
        auto& form = pair.second;
        if (form.filename.size() != 0) {
            if (form.content.size() == 0) {
                FILE* fp = fopen(form.filename.c_str(), "r");
                if (fp) {
                    struct stat st;
                    if (stat(form.filename.c_str(), &st) == 0 && st.st_size != 0) {
                        form.content.resize(st.st_size);
                        fread((void*)form.content.data(), 1, st.st_size, fp);
                    }
                    fclose(fp);
                }
            }
            snprintf(c_str, sizeof(c_str), "; filename=\"%s\"", basename(form.filename).c_str());
            str += c_str;
            const char* suffix = strrchr(form.filename.c_str(), '.');
            if (suffix) {
                const char* stype = http_content_type_str_by_suffix(++suffix);
                if (stype && *stype != '\0') {
                    str += "\r\n";
                    str += "Content-Type: ";
                    str += stype;
                }
            }
        }
        str += "\r\n\r\n";
        str += form.content;
        str += "\r\n";
    }
    str += "--";
    str += boundary;
    str += "--";
    return str;
}

#include "multipart_parser.h"
enum multipart_parser_state_e {
    MP_START,
    MP_PART_DATA_BEGIN,
    MP_HEADER_FIELD,
    MP_HEADER_VALUE,
    MP_HEADERS_COMPLETE,
    MP_PART_DATA,
    MP_PART_DATA_END,
    MP_BODY_END
};
struct multipart_parser_userdata {
    MultiPart* mp;
    // tmp
    multipart_parser_state_e state;
    std::string header_field;
    std::string header_value;
    std::string part_data;
    std::string name;
    std::string filename;

    void handle_header() {
        if (header_field.size() == 0 || header_value.size() == 0) return;
        if (stricmp(header_field.c_str(), "Content-Disposition") == 0) {
            StringList strlist = split(header_value, ';');
            for (auto& str : strlist) {
                StringList kv = split(trim(str, " "), '=');
                if (kv.size() == 2) {
                    const char* key = kv.begin()->c_str();
                    const char* value = trim_pairs(*(kv.begin()+1), "\"\"").c_str();
                    if (strcmp(key, "name") == 0) {
                        name = value;
                    }
                    else if (strcmp(key, "filename") == 0) {
                        filename = value;
                    }
                }
            }
        }
        header_field.clear();
        header_value.clear();
    }

    void handle_data() {
        if (name.size() != 0) {
            (*mp)[name] = FormData(part_data.c_str(), filename.c_str());
        }
        name.clear();
        filename.clear();
        part_data.clear();
    }
};
static int on_header_field(multipart_parser* parser, const char *at, size_t length) {
    //printf("on_header_field:%.*s\n", (int)length, at);
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->handle_header();
    userdata->state = MP_HEADER_FIELD;
    userdata->header_field.insert(userdata->header_field.size(), at, length);
    return 0;
}
static int on_header_value(multipart_parser* parser, const char *at, size_t length) {
    //printf("on_header_value:%.*s\n", (int)length, at);
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_HEADER_VALUE;
    userdata->header_value.insert(userdata->header_value.size(), at, length);
    return 0;
}
static int on_part_data(multipart_parser* parser, const char *at, size_t length) {
    //printf("on_part_data:%.*s\n", (int)length, at);
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_PART_DATA;
    userdata->part_data.insert(userdata->part_data.size(), at, length);
    return 0;
}
static int on_part_data_begin(multipart_parser* parser) {
    //printf("on_part_data_begin\n");
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_PART_DATA_BEGIN;
    return 0;
}
static int on_headers_complete(multipart_parser* parser) {
    //printf("on_headers_complete\n");
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->handle_header();
    userdata->state = MP_HEADERS_COMPLETE;
    return 0;
}
static int on_part_data_end(multipart_parser* parser) {
    //printf("on_part_data_end\n");
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_PART_DATA_END;
    userdata->handle_data();
    return 0;
}
static int on_body_end(multipart_parser* parser) {
    //printf("on_body_end\n");
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_BODY_END;
    return 0;
}
int parse_multipart(std::string& str, MultiPart& mp, const char* boundary) {
    //printf("boundary=%s\n", boundary);
    std::string __boundary("--");
    __boundary += boundary;
    multipart_parser_settings settings;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_part_data    = on_part_data;
    settings.on_part_data_begin  = on_part_data_begin;
    settings.on_headers_complete = on_headers_complete;
    settings.on_part_data_end    = on_part_data_end;
    settings.on_body_end         = on_body_end;
    multipart_parser* parser = multipart_parser_init(__boundary.c_str(), &settings);
    multipart_parser_userdata userdata;
    userdata.state = MP_START;
    userdata.mp = &mp;
    multipart_parser_set_data(parser, &userdata);
    size_t nparse = multipart_parser_execute(parser, str.c_str(), str.size());
    multipart_parser_free(parser);
    return nparse == str.size() ? 0 : -1;
}
#endif
