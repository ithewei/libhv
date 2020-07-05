#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "consul.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: consul_cli subcmd ServiceName [ServiceAddress ServicePort] [NodeIP NodePort]\n");
        printf("subcmd=[register,deregister,discover]\n");
        return -10;
    }
    const char* subcmd = argv[1];
    const char* ServiceName = argv[2];
    const char* ServiceAddress = "127.0.0.1";
    int ServicePort = 0;
    const char* NodeIP = "127.0.0.1";
    int NodePort = 8500;
    if (argc > 3) {
        ServiceAddress = argv[3];
    }
    if (argc > 4) {
        ServicePort = atoi(argv[4]);
    }
    if (argc > 5) {
        NodeIP = argv[5];
    }
    if (argc > 6) {
        NodePort = atoi(argv[6]);
    }

    consul_node_t node;
    strncpy(node.ip, NodeIP, sizeof(node.ip));
    node.port = NodePort;

    consul_service_t service;
    strncpy(service.name, ServiceName, sizeof(service.name));
    strncpy(service.ip, ServiceAddress, sizeof(service.ip));
    service.port = ServicePort;

    consul_health_t health;

    if (strcmp(subcmd, "register") == 0) {
        int ret = register_service(&node, &service, &health);
        printf("register_service retval=%d\n", ret);
        goto discover;
    }
    else if (strcmp(subcmd, "deregister") == 0) {
        int ret = deregister_service(&node, &service);
        printf("deregister_service retval=%d\n", ret);
        goto discover;
    }
    else if (strcmp(subcmd, "discover") == 0) {
discover:
        std::vector<consul_service_t> services;
        discover_services(&node, ServiceName, services);
        for (auto& service : services) {
            printf("name=%s ip=%s port=%d\n", service.name, service.ip, service.port);
        }
    }
    else {
        printf("subcmd error!\n");
        return -20;
    }

    return 0;
}
