#include <stdio.h>
#include <string.h>

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

    // test switch log file after the first write
    hlog_set_file("hlog_test_switch.log");
    hlogi("%s", "show info in the switched file");
    const char* curfile = hlog_get_cur_file();
    if (strstr(curfile, "hlog_test_switch") == NULL) {
        fprintf(stderr, "hlog_set_file did not switch the log file: %s\n", curfile);
        return 1;
    }

    return 0;
}
