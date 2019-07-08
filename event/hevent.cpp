#include "hevent.h"

#include "hdef.h"
#include "hlog.h"
#include "hsocket.h"

int _on_read(hevent_t* event) {
    event->readable = 1;
    //if (event->accept) {
    //}
    if (event->read_cb) {
        event->read_cb(event, event->read_userdata);
    }
    event->readable = 0;
    return 0;
}

int _on_write(hevent_t* event) {
    // ONESHOT
    _del_event(event, WRITE_EVENT);
    event->writeable = 1;
    //if (event->connect) {
    //}
    if (event->write_cb) {
        event->write_cb(event, event->read_userdata);
    }
    event->writeable = 0;
    return 0;
}

int hloop_event_init(hloop_t* loop) {
    return _event_init(loop);
}

int hloop_event_cleanup(hloop_t* loop) {
    return _event_cleanup(loop);
}

int hloop_add_event(hevent_t* event, int type) {
    return _add_event(event, type);
}

int hloop_del_event(hevent_t* event, int type) {
    return _del_event(event, type);
}

static void remove_bad_fds(hloop_t* loop) {
    int error = 0;
    socklen_t optlen = sizeof(int);
    int ret = 0;
    auto iter = loop->events.begin();
    while (iter != loop->events.end()) {
        int fd = iter->first;
        ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &optlen);
        if (ret < 0 || error != 0) {
            hloge("getsockopt fd=%d retval=%d SO_ERROR=%d", fd, ret, error);
            hloop_del_event(iter->second);
            iter = loop->events.erase(iter);
            continue;
        }
        ++iter;
    }
}

int hloop_handle_events(hloop_t* loop, int timeout) {
    /*
    // remove destroy events
    hevent_t* event = NULL;
    auto iter = loop->events.begin();
    while (iter != loop->events.end()) {
        event = iter->second;
        if (event->destroy) {
            SAFE_FREE(event);
            iter = loop->events.erase(iter);
            continue;
        }
        ++iter;
    }
    */
    int nevent = _handle_events(loop, timeout);
    if (nevent < 0) {
        printf("handle_events error=%d\n", -nevent);
        remove_bad_fds(loop);
    }
    return nevent;
}
