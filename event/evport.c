#include "iowatcher.h"

#ifdef EVENT_PORT

#include <port.h>

int iowatcher_init(hloop_t* loop) {
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    return 0;
}
#endif
