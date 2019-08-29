#include "hsocket.h"
#include "htime.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: cmd ip port\n");
        return -10;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    uint64_t start_time = gethrtime();
    int ret = ConnectNonblock(ip, port);
    uint64_t end_time = gethrtime();
    printf("ConnectNonblock[%s:%d] retval=%d cost=%luus\n", ip, port, ret, end_time-start_time);

    start_time = gethrtime();
    ret = ConnectTimeout(ip, port, 3000);
    end_time = gethrtime();
    printf("ConnectTimeout[%s:%d] retval=%d cost=%luus\n", ip, port, ret, end_time-start_time);

    start_time = gethrtime();
    ret = Connect(ip, port, 0);
    end_time = gethrtime();
    printf("ConnectBlock[%s:%d] retval=%d cost=%luus\n", ip, port, ret, end_time-start_time);

    return 0;
};
