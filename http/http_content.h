#ifndef HTTP_CONTENT_H_
#define HTTP_CONTENT_H_

#include <string>
#include <map>

// MultiMap
namespace std {
/*
int main() {
    std::MultiMap<std::string, std::string> kvs;
    kvs["name"] = "hw";
    kvs["filename"] = "1.jpg";
    kvs["filename"] = "2.jpg";
    //kvs.insert(std::pair<std::string,std::string>("name", "hw"));
    //kvs.insert(std::pair<std::string,std::string>("filename", "1.jpg"));
    //kvs.insert(std::pair<std::string,std::string>("filename", "2.jpg"));
    for (auto& pair : kvs) {
        printf("%s:%s\n", pair.first.c_str(), pair.second.c_str());
    }
    auto iter = kvs.find("filename");
    if (iter != kvs.end()) {
        for (int i = 0; i < kvs.count("filename"); ++i, ++iter) {
            printf("%s:%s\n", iter->first.c_str(), iter->second.c_str());
        }
    }
    return 0;
}
 */
template<typename Key,typename Value>
class MultiMap : public multimap<Key, Value> {
public:
    Value& operator[](Key key) {
        auto iter = this->insert(std::pair<Key,Value>(key,Value()));
        return (*iter).second;
    }
};
}

// MAP
#ifdef USE_MULTIMAP
#define MAP     std::MultiMap
#else
#define MAP     std::map
#endif

// KeyValue
typedef MAP<std::string, std::string> KeyValue;

// QueryParams
typedef KeyValue    QueryParams;
std::string dump_query_params(QueryParams& query_params);
int         parse_query_params(const char* query_string, QueryParams& query_params);

// NOTE: WITHOUT_HTTP_CONTENT
// ndk-r10e no std::to_string and can't compile modern json.hpp
#ifndef WITHOUT_HTTP_CONTENT
#include <sstream>

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
        // NOTE: low-version NDK not provide std::to_string
        //content = std::to_string(num);
        std::ostringstream os;
        os << num;
        content = os.str();
    }
};

// Multipart
// name => FormData
typedef MAP<std::string, FormData>          MultiPart;
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
