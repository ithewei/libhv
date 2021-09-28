#include "hstring.h"
using namespace hv;

int main(int argc, char** argv) {
    std::string str1 = "a1B2*C3d4==";
    std::string str2 = "a1B2*C3d4==";
    printf("toupper %s\n", toupper(str1).c_str());
    printf("tolower %s\n", tolower(str2).c_str());
    std::string str3 = "abcdefg";
    printf("reverse %s\n", reverse(str3).c_str());

    std::string str4 = "123456789";
    printf("startswith=%d\nendswith=%d\ncontains=%d\n",
        (int)startswith(str4, "123"),
        (int)endswith(str4, "789"),
        (int)contains(str4, "456"));

    std::string str5 = asprintf("%s%d", "hello", 5);
    printf("asprintf %s\n", str5.c_str());

    std::string str6("123,456,789");
    StringList strlist = split(str6, ',');
    printf("split %s\n", str6.c_str());
    for (auto& str : strlist) {
        printf("%s\n", str.c_str());
    }

    std::string str7("user=admin&pswd=123456");
    hv::KeyValue kv = splitKV(str7, '&', '=');
    for (auto& pair : kv) {
        printf("%s=%s\n", pair.first.c_str(), pair.second.c_str());
    }

    std::string str8("<stdio.h>");
    std::string str9 = trim_pairs(str8);
    printf("trim_pairs %s\n", str9.c_str());

    std::string str10("<title>{{title}}</title>");
    std::string str11 = replace(str10, "{{title}}", "Home");
    printf("replace %s\n", str11.c_str());

    return 0;
}
