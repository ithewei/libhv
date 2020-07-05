#ifndef HTTP_CONTENT_H_
#define HTTP_CONTENT_H_

#include "hstring.h"

// QueryParams
typedef hv::KeyValue    QueryParams;
std::string dump_query_params(QueryParams& query_params);
int         parse_query_params(const char* query_string, QueryParams& query_params);

// NOTE: WITHOUT_HTTP_CONTENT
// ndk-r10e no std::to_string and can't compile modern json.hpp
#ifndef WITHOUT_HTTP_CONTENT

/**************multipart/form-data*************************************
--boundary
Content-Disposition: form-data; name="user"

content
--boundary
Content-Disposition: form-data; name="avatar"; filename="user.jpg"
Content-Type: image/jpeg

content
--boundary--
***********************************************************************/
// FormData
struct FormData {
    std::string     filename;
    std::string     content;

    FormData(const char* content = NULL, const char* filename = NULL) {
        if (content) {
            this->content = content;
        }
        if (filename) {
            this->filename = filename;
        }
    }
    template<typename T>
    FormData(T num) {
        content = hv::to_string(num);
    }
};

// Multipart
// name => FormData
typedef HV_MAP<std::string, FormData>          MultiPart;
#define DEFAULT_MULTIPART_BOUNDARY  "----WebKitFormBoundary7MA4YWxkTrZu0gW"
std::string dump_multipart(MultiPart& mp, const char* boundary = DEFAULT_MULTIPART_BOUNDARY);
int         parse_multipart(std::string& str, MultiPart& mp, const char* boundary);

// Json
#include "json.hpp"
using Json = nlohmann::json;
extern std::string g_parse_json_errmsg;
std::string dump_json(Json& json);
int         parse_json(const char* str, Json& json, std::string& errmsg = g_parse_json_errmsg);
#endif

#endif // HTTP_CONTENT_H_
