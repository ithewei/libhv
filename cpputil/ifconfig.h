#ifndef HV_IFCONFIG_H_
#define HV_IFCONFIG_H_

#include <vector>

#include "hexport.h"

#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

typedef struct ifconfig_s {
    char name[128];
    char ip[16];
    char mask[16];
    char broadcast[16];
    char mac[20];
} ifconfig_t;

/*
 *  @test
    std::vector<ifconfig_t> ifcs;
    ifconfig(ifcs);
    for (auto& item : ifcs) {
        printf("%s\nip: %s\nmask: %s\nbroadcast: %s\nmac: %s\n\n",
                item.name,
                item.ip,
                item.mask,
                item.broadcast,
                item.mac);
    }
 */
HV_EXPORT int ifconfig(std::vector<ifconfig_t>& ifcs);

#endif // HV_IFCONFIG_H_
