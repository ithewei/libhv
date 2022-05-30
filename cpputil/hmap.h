#ifndef HV_MAP_H_
#define HV_MAP_H_

#include "hconfig.h"

#include <map>
#include <string>

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

#ifdef USE_MULTIMAP
#define HV_MAP      std::MultiMap
#else
#define HV_MAP      std::map
#endif

// KeyValue
namespace hv {
typedef std::map<std::string, std::string>      keyval_t;
typedef std::MultiMap<std::string, std::string> multi_keyval_t;
typedef HV_MAP<std::string, std::string>        KeyValue;
}

#endif // HV_MAP_H_
