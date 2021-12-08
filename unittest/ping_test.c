#include <stdio.h>
#include "icmp.h"
#include "hplatform.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: ping host|ip\n");
        return -10;
    }

    char* host = argv[1];
    int ping_cnt = 4;
    int ok_cnt = ping(host, ping_cnt);
    printf("ping %d count, %d ok.\n", ping_cnt, ok_cnt);
    return 0;
}
