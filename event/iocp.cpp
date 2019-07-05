#include "hevent.h"

#ifdef EVENT_IOCP
int _event_init(hloop_t* loop) {
    loop->event_ctx = NULL;
    return 0;
}

int _event_cleanup(hloop_t* loop) {
    loop->event_ctx = NULL;
    return 0;
}

int _add_event(hevent_t* event, int type) {
    return 0;
}

int _del_event(hevent_t* event, int type) {
    return 0;
}

int _handle_events(hloop_t* loop, int timeout) {
    return 0;
}
#endif
