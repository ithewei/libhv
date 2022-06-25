#include <assert.h>

#include "hurl.h"

int main(int argc, char** argv) {
    std::string strURL = "http://www.example.com/path?query#fragment";
    HUrl url;
    if (!url.parse(strURL)) {
        printf("parse url %s error!\n", strURL.c_str());
        return -1;
    }
    std::string dumpURL = url.dump();
    printf("%s =>\n%s\n", strURL.c_str(), dumpURL.c_str());
    assert(strURL == dumpURL);

    const char* str = "中 文";
    std::string escaped = HUrl::escape(str);
    std::string unescaped = HUrl::unescape(escaped.c_str());
    printf("%s => %s\n", str, escaped.c_str());
    assert(str == unescaped);

    return 0;
}
