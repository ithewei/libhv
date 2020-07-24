#include "hstring.h"

int main(int argc, char** argv) {
    /*
    char str1[] = "a1B2*C3d4==";
    char str2[] = "a1B2*C3d4==";
    printf("strupper %s\n", strupper(str1));
    printf("strlower %s\n", strlower(str2));
    char str3[] = "abcdefg";
    printf("strreverse %s\n", strreverse(str3));

    char str4[] = "123456789";
    printf("strstartswith=%d\nstrendswith=%d\nstrcontains=%d\n",
        (int)strstartswith(str4, "123"),
        (int)strendswith(str4, "789"),
        (int)strcontains(str4, "456"));
    */

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

    std::string filepath("/mnt/share/image/test.jpg");
    std::string base = basename(filepath);
    std::string dir = dirname(filepath);
    std::string file = filename(filepath);
    std::string suffix = suffixname(filepath);
    printf("filepath %s\n", filepath.c_str());
    printf("basename %s\n", base.c_str());
    printf("dirname %s\n", dir.c_str());
    printf("filename %s\n", file.c_str());
    printf("suffixname %s\n", suffix.c_str());

    return 0;
}
