#ifndef HV_RUDP_H_
#define HV_RUDP_H_

#include "hloop.h"

#if WITH_RUDP

#include "rbtree.h"
#include "hsocket.h"
#include "hmutex.h"
#if WITH_KCP
#include "kcp/hkcp.h"
#endif

typedef struct rudp_s {
    struct rb_root  rb_root;
    hmutex_t        mutex;
} rudp_t;

typedef struct rudp_entry_s {
    struct rb_node  rb_node;
    sockaddr_u      addr; // key
    // val
    hio_t*          io;
#if WITH_KCP
    kcp_t           kcp;
#endif
} rudp_entry_t;

// NOTE: rudp_entry_t alloc when rudp_get
void rudp_entry_free(rudp_entry_t* entry);

void rudp_init(rudp_t* rudp);
void rudp_cleanup(rudp_t* rudp);

bool rudp_insert(rudp_t* rudp, rudp_entry_t* entry);
// NOTE: just rb_erase, not free
rudp_entry_t* rudp_remove(rudp_t* rudp, struct sockaddr* addr);
rudp_entry_t* rudp_search(rudp_t* rudp, struct sockaddr* addr);
#define rudp_has(rudp, addr) (rudp_search(rudp, addr) != NULL)

// rudp_search + malloc + rudp_insert
rudp_entry_t* rudp_get(rudp_t* rudp, struct sockaddr* addr);
// rudp_remove + free
void          rudp_del(rudp_t* rudp, struct sockaddr* addr);

// rudp_get(&io->rudp, io->peeraddr)
rudp_entry_t* hio_get_rudp(hio_t* io);

#endif // WITH_RUDP

#endif // HV_RUDP_H_
