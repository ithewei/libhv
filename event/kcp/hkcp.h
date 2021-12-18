#ifndef HV_KCP_H_
#define HV_KCP_H_

#include "hloop.h"

#if WITH_KCP

#include "ikcp.h"
#include "hbuf.h"

#define DEFAULT_KCP_UPDATE_INTERVAL 10 // ms
#define DEFAULT_KCP_READ_BUFSIZE    1400

typedef struct kcp_s {
    ikcpcb*         ikcp;
    uint32_t        conv;
    htimer_t*       update_timer;
    hbuf_t          readbuf;
} kcp_t;

// NOTE: kcp_create in hio_get_kcp
void kcp_release(kcp_t* kcp);

kcp_t* hio_get_kcp  (hio_t* io, uint32_t conv);
int    hio_read_kcp (hio_t* io, void* buf, int readbytes);
int    hio_write_kcp(hio_t* io, const void* buf, size_t len);

#endif

#endif // HV_KCP_H_
