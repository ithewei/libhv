#include "hbase.h"

int main(int argc, char* argv[]) {
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

    char buf[16] = {0};
    printf("%d\n", hv_rand(10, 99));
    printf("%s\n", hv_random_string(buf, 10));

    return 0;
}
