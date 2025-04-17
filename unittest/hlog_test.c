#include "hlog.h"

int main(int argc, char* argv[]) {
    char logfile[] = "hlog_test.log";
    hlog_set_file(logfile);
    hlog_set_level(LOG_LEVEL_INFO);

    // test log max filesize
    hlog_set_max_filesize_by_str("1M");
    for (int i = 100000; i <= 999999; ++i) {
        hlogi("[%d] xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", i);
    }

    // test log level
    hlogd("%s", "not show debug");
    hlogi("%s", "show info");
    hlogw("%s", "show warn");
    hloge("%s", "show error");
    hlogf("%s", "show fatal");

    return 0;
}
