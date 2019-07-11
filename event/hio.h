#ifndef HW_IO_H_
#define HW_IO_H_

#include "hloop.h"

hio_t* hio_get(hloop_t* loop, int fd);
hio_t* hio_add(hloop_t* loop, int fd);
void   hio_del(hio_t* io);
int    hio_handle_events(hio_t* io);

hio_t* hio_read   (hloop_t* loop, int fd, hio_cb revent_cb, void* revent_userdata);
hio_t* hio_write  (hloop_t* loop, int fd, hio_cb wevent_cb, void* wevent_userdata);
hio_t* hio_accept (hloop_t* loop, int listenfd, hio_cb revent_cb, void* revent_userdata);
hio_t* hio_connect(hloop_t* loop, int connfd, hio_cb wevent_cb, void* wevent_userdata);

#endif // HW_IO_H_
