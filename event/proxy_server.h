#ifndef HV_PROXY_SERVER_H_
#define HV_PROXY_SERVER_H_

#include "hloop.h"

typedef enum {
    PROXY_SERVER_TCP,
    PROXY_SERVER_UDP,
    PROXY_SERVER_SOCKS5,
} proxy_server_type_e;

typedef struct proxy_server_s {
    proxy_setting_t      setting;
    proxy_server_type_e  type;
    unsigned int         refcount;
} proxy_server_t;

struct hio_s;

proxy_server_t* proxy_server_retain(proxy_server_t* server);
void proxy_server_release(proxy_server_t* server);

#endif // HV_PROXY_SERVER_H_
