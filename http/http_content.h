#ifndef HV_HTTP_CONTENT_H_
#define HV_HTTP_CONTENT_H_

#include "hexport.h"
#include "hstring.h"

// QueryParams
typedef hv::KeyValue    QueryParams;
HV_EXPORT std::string dump_query_params(const QueryParams& query_params);
HV_EXPORT int         parse_query_params(const char* query_string, QueryParams& query_params);

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
// FormFile
struct FormFile : public FormData {
    FormFile(const char* filename = NULL) {
        if (filename) {
            this->filename = filename;
        }
    }
};

// MultiPart
// name => FormData
typedef HV_MAP<std::string, FormData>          MultiPart;
#define DEFAULT_MULTIPART_BOUNDARY  "----WebKitFormBoundary7MA4YWxkTrZu0gW"
HV_EXPORT std::string dump_multipart(MultiPart& mp, const char* boundary = DEFAULT_MULTIPART_BOUNDARY);
HV_EXPORT int         parse_multipart(const std::string& str, MultiPart& mp, const char* boundary);

// Json
// https://github.com/nlohmann/json
#include "json.hpp"
namespace hv { // NOTE: Avoid conflict with jsoncpp
using Json = nlohmann::json;
// using Json = nlohmann::ordered_json;
}

HV_EXPORT std::string dump_json(const hv::Json& json, int indent = -1);
HV_EXPORT int         parse_json(const char* str, hv::Json& json, std::string& errmsg);
#endif

#endif // HV_HTTP_CONTENT_H_
