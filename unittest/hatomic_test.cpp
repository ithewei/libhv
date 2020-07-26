#include <stdio.h>

#include "hatomic.h"
#include "hthread.h"

hatomic_flag_t flag = HATOMIC_FLAG_INIT;
hatomic_t cnt = HATOMIC_VAR_INIT(0);

HTHREAD_ROUTINE(test_hatomic_flag) {
    if (!hatomic_flag_test_and_set(&flag)) {
        printf("tid=%ld flag 0=>1\n", hv_gettid());
    }
    else {
        printf("tid=%ld flag=1\n", hv_gettid());
    }
    return 0;
}

HTHREAD_ROUTINE(test_hatomic) {
    for (int i = 0; i < 10; ++i) {
        long n = hatomic_inc(&cnt);
        printf("tid=%ld cnt=%ld\n", hv_gettid(), n);
        hv_delay(1);
    }
    return 0;
}

int main() {
    for (int i = 0; i < 10; ++i) {
        hthread_create(test_hatomic_flag, NULL);
    }

    for (int i = 0; i < 10; ++i) {
        hthread_create(test_hatomic, NULL);
    }

    hv_delay(1000);
    return 0;
}
