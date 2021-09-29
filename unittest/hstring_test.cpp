#include "hstring.h"
using namespace hv;

int main(int argc, char** argv) {
    std::string str1 = "a1B2*C3d4==";
    std::string str2 = "a1B2*C3d4==";
    println("toupper=" + toupper(str1));
    println("tolower=" + tolower(str2));
    std::string str3 = "abcdefg";
    println("reverse=" + reverse(str3));

    std::string str4 = "123456789";
    printf("startswith=%d\nendswith=%d\ncontains=%d\n",
        (int)startswith(str4, "123"),
        (int)endswith(str4, "789"),
        (int)contains(str4, "456"));

    std::string str5 = asprintf("%s%d", "hello", 5);
    println("asprintf=" + str5);

    std::string str6("123,456,789");
    StringList strlist = split(str6, ',');
    println("split " + str6);
    for (auto& str : strlist) {
        println(str);
    }

    std::string str7("user=admin&pswd=123456");
    hv::KeyValue kv = splitKV(str7, '&', '=');
    for (auto& pair : kv) {
        printf("%s=%s\n", pair.first.c_str(), pair.second.c_str());
    }

    std::string str8("<stdio.h>");
    std::string str9 = trim_pairs(str8);
    println("trim_pairs=" + str9);

    std::string str10("<title>{{title}}</title>");
    std::string str11 = replace(str10, "{{title}}", "Home");
    println("replace=" + str11);

    return 0;
}
