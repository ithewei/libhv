#include "iowatcher.h"

#ifdef EVENT_IOCP
#include "hplatform.h"

hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb) {
    return NULL;
}

hio_t* hconnect (hloop_t* loop, int connfd, hconnect_cb connect_cb) {
    return NULL;
}

hio_t* hread    (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb) {
    return NULL;
}

hio_t* hwrite   (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb cb) {
    return NULL;
}

void   hclose   (hio_t* io) {

}

#endif
