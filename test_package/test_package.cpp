#include "hv/hv.h"
#include <stdio.h>

int main() {
    printf("libhv version: %s\n", hv_version());
    return 0;
}
