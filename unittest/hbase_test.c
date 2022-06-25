#include "hbase.h"

int main(int argc, char* argv[]) {
    char buf[16] = {0};
    printf("hv_rand(10, 99) -> %d\n", hv_rand(10, 99));
    printf("hv_random_string(buf, 10) -> %s\n", hv_random_string(buf, 10));

    assert(hv_getboolean("1"));
    assert(hv_getboolean("yes"));

    assert(hv_parse_size("256") == 256);
    assert(hv_parse_size("1K") == 1024);
    assert(hv_parse_size("1G2M3K4B") ==
            1 * 1024 * 1024 * 1024 +
            2 * 1024 * 1024 +
            3 * 1024 +
            4);

    assert(hv_parse_time("30") == 30);
    assert(hv_parse_time("1m") == 60);
    assert(hv_parse_time("1d2h3m4s") ==
            1 * 24 * 60 * 60 +
            2 * 60 * 60 +
            3 * 60 +
            4);

    const char* test_urls[] = {
        "http://user:pswd@www.example.com:80/path?query#fragment",
        "http://user:pswd@www.example.com/path?query#fragment",
        "http://www.example.com/path?query#fragment",
        "http://www.example.com/path?query",
        "http://www.example.com/path",
        "www.example.com/path",
        "/path",
    };
    hurl_t stURL;
    for (int i = 0; i < ARRAY_SIZE(test_urls); ++i) {
        const char* strURL = test_urls[i];
        printf("%s =>\n", strURL);
        hv_parse_url(&stURL, strURL);
        assert(stURL.port == 80);
        // scheme://
        if (stURL.fields[HV_URL_SCHEME].len > 0) {
            const char* scheme = strURL + stURL.fields[HV_URL_SCHEME].off;
            int len = stURL.fields[HV_URL_SCHEME].len;
            assert(len == 4);
            assert(strncmp(scheme, "http", len) == 0);
            printf("%.*s://", len, scheme);
        }
        // user:pswd@
        if (stURL.fields[HV_URL_USERNAME].len > 0) {
            const char* user = strURL + stURL.fields[HV_URL_USERNAME].off;
            int len = stURL.fields[HV_URL_USERNAME].len;
            assert(len == 4);
            assert(strncmp(user, "user", len) == 0);
            printf("%.*s", len, user);
            if (stURL.fields[HV_URL_PASSWORD].len > 0) {
                const char* pswd = strURL + stURL.fields[HV_URL_PASSWORD].off;
                int len = stURL.fields[HV_URL_PASSWORD].len;
                assert(len == 4);
                assert(strncmp(pswd, "pswd", len) == 0);
                printf(":%.*s", len, pswd);
            }
            printf("@");
        }
        // host:port
        if (stURL.fields[HV_URL_HOST].len > 0) {
            const char* host = strURL + stURL.fields[HV_URL_HOST].off;
            int len = stURL.fields[HV_URL_HOST].len;
            assert(len == strlen("www.example.com"));
            assert(strncmp(host, "www.example.com", len) == 0);
            printf("%.*s", len, host);
            if (stURL.fields[HV_URL_PORT].len > 0) {
                const char* port = strURL + stURL.fields[HV_URL_PORT].off;
                int len = stURL.fields[HV_URL_PORT].len;
                assert(len == 2);
                assert(strncmp(port, "80", len) == 0);
                printf(":%.*s", len, port);
            }
        }
        // /path
        if (stURL.fields[HV_URL_PATH].len > 0) {
            const char* path = strURL + stURL.fields[HV_URL_PATH].off;
            int len = stURL.fields[HV_URL_PATH].len;
            assert(len == 5);
            assert(strncmp(path, "/path", len) == 0);
            printf("%.*s", len, path);
        }
        // ?query
        if (stURL.fields[HV_URL_QUERY].len > 0) {
            const char* query = strURL + stURL.fields[HV_URL_QUERY].off;
            int len = stURL.fields[HV_URL_QUERY].len;
            assert(len == 5);
            assert(strncmp(query, "query", len) == 0);
            printf("?%.*s", len, query);
        }
        // #fragment
        if (stURL.fields[HV_URL_FRAGMENT].len > 0) {
            const char* fragment = strURL + stURL.fields[HV_URL_FRAGMENT].off;
            int len = stURL.fields[HV_URL_FRAGMENT].len;
            assert(len == 8);
            assert(strncmp(fragment, "fragment", len) == 0);
            printf("#%.*s", len, fragment);
        }
        printf("\n");
    }

    return 0;
}
