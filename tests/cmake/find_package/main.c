#include "hv/hv.h"

int main(void) {
    return hv_getboolean("true") ? 0 : 1;
}
