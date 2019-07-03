#ifndef IFCONFIG_H_
#define IFCONFIG_H_

#include <vector>

typedef struct ifconfig_s {
    char name[128];
    char ip[32];
    char mask[32];
    char mac[32];
} ifconfig_t;

/*
 *  @test
    std::vector<ifconfig_t> ifcs;
    ifconfig(ifcs);
    for (auto& item : ifcs) {
        printf("%s\nip: %s\nmask: %s\nmac: %s\n\n",
                item.name,
                item.ip,
                item.mask,
                item.mac);
    }
 */
int ifconfig(std::vector<ifconfig_t>& ifcs);

#endif // IFCONFIG_H_
