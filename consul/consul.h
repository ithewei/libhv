#ifndef CONSUL_H_
#define CONSUL_H_

#include <vector>
#include <string.h>

#include "hexport.h"

typedef struct consul_node_s {
    // node
    char ip[32];
    int  port;

    consul_node_s() {
        strcpy(ip, "127.0.0.1");
        port = 8500;
    }
} consul_node_t;

typedef struct consul_service_s {
    // service
    char name[64];
    char ip[32];
    int  port;

    consul_service_s() {
        memset(this, 0, sizeof(consul_service_s));
        strcpy(ip, "127.0.0.1");
    }
} consul_service_t;

typedef struct consul_health_s {
    // check
    char protocol[32]; // TCP,HTTP
    char url[256];
    char status[32]; // any,passing,warning,critical

    int interval; // ms
    int timeout;  // ms

    consul_health_s() {
        memset(this, 0, sizeof(consul_health_s));
        strcpy(protocol, "TCP");
        strcpy(status, "passing");
        interval = 10000;
        timeout = 3000;
    }
} consul_health_t;

HV_EXPORT int register_service(consul_node_t* node, consul_service_t* service, consul_health_t* health);
HV_EXPORT int deregister_service(consul_node_t* node, consul_service_t* service);
HV_EXPORT int discover_services(consul_node_t* node, const char* service_name, std::vector<consul_service_t>& services);

#endif // CONSUL_H_
