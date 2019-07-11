#include "io_watcher.h"
#include "hio.h"

#include "hdef.h"
#include "hlog.h"
#include "hsocket.h"

int hloop_iowatcher_init(hloop_t* loop) {
    return iowatcher_init(loop);
}

int hloop_iowatcher_cleanup(hloop_t* loop) {
    return iowatcher_cleanup(loop);
}

static void remove_bad_fds(hloop_t* loop) {
    int error = 0;
    socklen_t optlen = sizeof(int);
    int ret = 0;
    auto iter = loop->ios.begin();
    while (iter != loop->ios.end()) {
        int fd = iter->first;
        ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &optlen);
        if (ret < 0 || error != 0) {
            hloge("getsockopt fd=%d retval=%d SO_ERROR=%d", fd, ret, error);
            hio_del(iter->second);
            continue;
        }
        ++iter;
    }
}

int hloop_handle_ios(hloop_t* loop, int timeout) {
    int nevent = iowatcher_poll_events(loop, timeout);
    if (nevent < 0) {
        hloge("poll_events error=%d", -nevent);
        remove_bad_fds(loop);
    }
    return nevent;
}
