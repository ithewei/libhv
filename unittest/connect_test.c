#include "hsocket.h"
#include "htime.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: cmd ip port\n");
        return -10;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    unsigned int start_time = gettick_ms();
    int ret = ConnectNonblock(ip, port);
    unsigned int end_time = gettick_ms();
    printf("ConnectNonblock[%s:%d] retval=%d cost=%ums\n", ip, port, ret, end_time-start_time);

    start_time = gettick_ms();
    ret = ConnectTimeout(ip, port, 3000);
    end_time = gettick_ms();
    printf("ConnectTimeout[%s:%d] retval=%d cost=%ums\n", ip, port, ret, end_time-start_time);

    start_time = gettick_ms();
    ret = Connect(ip, port, 0);
    end_time = gettick_ms();
    printf("ConnectBlock[%s:%d] retval=%d cost=%ums\n", ip, port, ret, end_time-start_time);

    return 0;
}
