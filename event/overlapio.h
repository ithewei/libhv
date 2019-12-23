#ifndef HV_OVERLAPPED_H_
#define HV_OVERLAPPED_H_

#include "iowatcher.h"

#ifdef EVENT_IOCP

#include "hbuf.h"
#include "hsocket.h"
#include <mswsock.h>
#ifdef _MSC_VER
#pragma comment(lib, "mswsock.lib")
#endif

typedef struct hoverlapped_s {
    OVERLAPPED  ovlp;
    int         fd;
    int         event;
    WSABUF      buf;
    int         bytes;
    int         error;
    hio_t*      io;
    // for recvfrom
    struct sockaddr* addr;
    int         addrlen;
} hoverlapped_t;

int post_acceptex(hio_t* listenio, hoverlapped_t* hovlp);
int post_recv(hio_t* io, hoverlapped_t* hovlp);

#endif

#endif // HV_OVERLAPPED_H_
