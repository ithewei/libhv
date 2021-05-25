#ifndef CONSUL_H_
#define CONSUL_H_

#include <vector>
#include <string.h>

typedef struct consul_node_s {
    // node
    char ip[64];
    int  port;

    consul_node_s() {
        strcpy(ip, "127.0.0.1");
        port = 8500;
    }
} consul_node_t;

typedef struct consul_service_s {
    // service
    char name[64];
    char ip[64];
    int  port;

    consul_service_s() {
        name[0] = '\0';
        strcpy(ip, "127.0.0.1");
        port = 0;
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
        strcpy(protocol, "TCP");
        url[0] = '\0';
        strcpy(status, "passing");
        interval = 10000;
        timeout = 3000;
    }
} consul_health_t;

int register_service(consul_node_t* node, consul_service_t* service, consul_health_t* health);
int deregister_service(consul_node_t* node, consul_service_t* service);
int discover_services(consul_node_t* node, const char* service_name, std::vector<consul_service_t>& services);

#endif // CONSUL_H_
