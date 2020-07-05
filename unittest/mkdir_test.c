#include "hbase.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: mkdir_p dir\n");
        return -1;
    }
    const char* dir = argv[1];
    return hv_mkdir_p(dir);
}
