#include "hloop.h"

static void on_ready(hevent_t *ev) {
    (void)ev;
}

int main(void) {
    hevent_t ev = {0};
    hevent_set_cb(&ev, on_ready);
    return hevent_cb(&ev) != on_ready;
}
