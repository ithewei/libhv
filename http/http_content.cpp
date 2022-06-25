#include "http_content.h"

#include "hurl.h"

#include <string.h>

BEGIN_NAMESPACE_HV

std::string dump_query_params(const QueryParams& query_params) {
    std::string query_string;
    for (auto& pair : query_params) {
        if (query_string.size() != 0) {
            query_string += '&';
        }
        query_string += HUrl::escape(pair.first);
        query_string += '=';
        query_string += HUrl::escape(pair.second);
    }
    return query_string;
}

int parse_query_params(const char* query_string, QueryParams& query_params) {
    const char* p = strchr(query_string, '?');
    p = p ? p+1 : query_string;

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
            if (key_len /* && value_len */) {
                std::string strkey = std::string(key, key_len);
                std::string strvalue = std::string(value, value_len);
                query_params[HUrl::unescape(strkey)] = HUrl::unescape(strvalue);
                key_len = value_len = 0;
            }
            state = s_key;
            key = p+1;
        }
        else if (*p == '=') {
            state = s_value;
            value = p+1;
        }
        else {
            state == s_key ? ++key_len : ++value_len;
        }
        ++p;
    }
    if (key_len /* && value_len */) {
        std::string strkey = std::string(key, key_len);
        std::string strvalue = std::string(value, value_len);
        query_params[HUrl::unescape(strkey)] = HUrl::unescape(strvalue);
        key_len = value_len = 0;
    }
    return query_params.size() == 0 ? -1 : 0;
}

#ifndef WITHOUT_HTTP_CONTENT

#include "hstring.h" // for split
#include "hfile.h"
#include "httpdef.h" // for http_content_type_str_by_suffix

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
                HFile file;
                if (file.open(form.filename.c_str(), "rb") == 0) {
                    file.readall(form.content);
                }
            }
            snprintf(c_str, sizeof(c_str), "; filename=\"%s\"", hv_basename(form.filename.c_str()));
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
                    std::string value = *(kv.begin() + 1);
                    value = trim_pairs(value, "\"\"\'\'");
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
            FormData formdata;
            formdata.content = part_data;
            formdata.filename = filename;
            (*mp)[name] = formdata;
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
    userdata->header_field.append(at, length);
    return 0;
}
static int on_header_value(multipart_parser* parser, const char *at, size_t length) {
    //printf("on_header_value:%.*s\n", (int)length, at);
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_HEADER_VALUE;
    userdata->header_value.append(at, length);
    return 0;
}
static int on_part_data(multipart_parser* parser, const char *at, size_t length) {
    //printf("on_part_data:%.*s\n", (int)length, at);
    multipart_parser_userdata* userdata = (multipart_parser_userdata*)multipart_parser_get_data(parser);
    userdata->state = MP_PART_DATA;
    userdata->part_data.append(at, length);
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
int parse_multipart(const std::string& str, MultiPart& mp, const char* boundary) {
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

std::string dump_json(const hv::Json& json, int indent) {
    return json.dump(indent);
}

int parse_json(const char* str, hv::Json& json, std::string& errmsg) {
    try {
        json = nlohmann::json::parse(str);
    }
    catch(nlohmann::detail::exception e) {
        errmsg = e.what();
        return -1;
    }
    return (json.is_discarded() || json.is_null()) ? -1 : 0;
}
#endif

END_NAMESPACE_HV
