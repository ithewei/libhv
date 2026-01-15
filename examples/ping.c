#include <stdio.h>

#ifndef PRINT_DEBUG
#define PRINT_DEBUG
#endif

#include "icmp.h"

int main(int argc, char const *argv[]) {
    if(argc > 1)
        ping(argv[1], 4);
    else
        fprintf(stderr, "usage: ping <host ip or address>\n");
    return 0;
}
