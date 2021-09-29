#include "htime.h"

int main(int argc, char* argv[]) {
    datetime_t dt = datetime_now();
    char buf1[DATETIME_FMT_BUFLEN];
    datetime_fmt(&dt, buf1);
    puts(buf1);
    datetime_fmt_iso(&dt, buf1);
    puts(buf1);

    time_t ts = datetime_mktime(&dt);
    char buf2[GMTIME_FMT_BUFLEN];
    gmtime_fmt(ts, buf2);
    puts(buf2);

    return 0;
}
