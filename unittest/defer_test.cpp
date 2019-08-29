#include <stdio.h>

#include "hscope.h"

int main() {
    defer (
        printf("1\n");
        printf("2\n");
    )

    defer (
        printf("3\n");
        printf("4\n");
    )

    defer(printf("hello\n");)

    return 0;
}
