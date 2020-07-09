#include "ifconfig.h"

#include "hplatform.h"

#ifdef OS_LINUX
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <arpa/inet.h>

int ifconfig(std::vector<ifconfig_t>& ifcs) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -10;
    }

    struct ifconf ifc;
    char buf[1024];
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    int iRet = ioctl(sock, SIOCGIFCONF, &ifc);
    if (iRet != 0) {
        close(sock);
        return iRet;
    }

    int cnt = ifc.ifc_len / sizeof(struct ifreq);
    //printf("ifc.size=%d\n", cnt);
    if (cnt == 0) {
        close(sock);
        return -20;
    }

    struct ifreq ifr;
    ifcs.clear();
    ifconfig_t tmp;
    for (int i = 0; i < cnt; ++i) {
        // name
        strcpy(ifr.ifr_name, ifc.ifc_req[i].ifr_name);
        //printf("name: %s\n", ifr.ifr_name);
        strncpy(tmp.name, ifr.ifr_name, sizeof(tmp.name));
        // flags
        //iRet = ioctl(sock, SIOCGIFFLAGS, &ifr);
        //short flags = ifr.ifr_flags;
        // addr
        iRet = ioctl(sock, SIOCGIFADDR, &ifr);
        struct sockaddr_in* addr = (struct sockaddr_in*)&ifr.ifr_addr;
        char* ip = inet_ntoa(addr->sin_addr);
        //printf("ip: %s\n", ip);
        strncpy(tmp.ip, ip, sizeof(tmp.ip));
        // netmask
        iRet = ioctl(sock, SIOCGIFNETMASK, &ifr);
        addr = (struct sockaddr_in*)&ifr.ifr_netmask;
        char* netmask = inet_ntoa(addr->sin_addr);
        //printf("netmask: %s\n", netmask);
        strncpy(tmp.mask, netmask, sizeof(tmp.mask));
        // broadaddr
        iRet = ioctl(sock, SIOCGIFBRDADDR, &ifr);
        addr = (struct sockaddr_in*)&ifr.ifr_broadaddr;
        char* broadaddr = inet_ntoa(addr->sin_addr);
        //printf("broadaddr: %s\n", broadaddr);
        strncpy(tmp.broadcast, broadaddr, sizeof(tmp.broadcast));
        // hwaddr
        iRet = ioctl(sock, SIOCGIFHWADDR, &ifr);
        snprintf(tmp.mac, sizeof(tmp.mac), "%02x:%02x:%02x:%02x:%02x:%02x",
            (unsigned char)ifr.ifr_hwaddr.sa_data[0],
            (unsigned char)ifr.ifr_hwaddr.sa_data[1],
            (unsigned char)ifr.ifr_hwaddr.sa_data[2],
            (unsigned char)ifr.ifr_hwaddr.sa_data[3],
            (unsigned char)ifr.ifr_hwaddr.sa_data[4],
            (unsigned char)ifr.ifr_hwaddr.sa_data[5]);
        //printf("mac: %s\n", tmp.mac);
        //printf("\n");

        if (strcmp(tmp.ip, "0.0.0.0") == 0 ||
            strcmp(tmp.ip, "127.0.0.1") == 0 ||
            strcmp(tmp.mac, "00:00:00:00:00:00") == 0) {
            continue;
        }

        ifcs.push_back(tmp);
    }

    close(sock);
    return 0;
}
#elif defined(OS_WIN)
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
int ifconfig(std::vector<ifconfig_t>& ifcs) {
    PIP_ADAPTER_ADDRESSES pAddrs = NULL;
    ULONG buflen = 0;
    GetAdaptersAddresses(AF_INET, 0, NULL, pAddrs, &buflen);
    pAddrs = (PIP_ADAPTER_ADDRESSES)malloc(buflen);
    GetAdaptersAddresses(AF_INET, 0, NULL, pAddrs, &buflen);

    PIP_ADAPTER_INFO pInfos = NULL;
    buflen = 0;
    GetAdaptersInfo(pInfos, &buflen);
    pInfos = (PIP_ADAPTER_INFO)malloc(buflen);
    GetAdaptersInfo(pInfos, &buflen);

    ifconfig_t ifc;
    std::vector<ifconfig_t> tmp_ifcs;
    PIP_ADAPTER_ADDRESSES pAddr = pAddrs;
    char mac[32] = {'\0'};
    while (pAddr) {
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
            pAddr->PhysicalAddress[0],
            pAddr->PhysicalAddress[1],
            pAddr->PhysicalAddress[2],
            pAddr->PhysicalAddress[3],
            pAddr->PhysicalAddress[4],
            pAddr->PhysicalAddress[5]);

        memset(&ifc, 0, sizeof(ifc));
        strncpy(ifc.name, pAddr->AdapterName, sizeof(ifc.name));
        strncpy(ifc.ip, "0.0.0.0", sizeof(ifc.ip));
        strncpy(ifc.mask, "255.255.255.255", sizeof(ifc.mask));
        strncpy(ifc.mac, mac, sizeof(ifc.mac));
        tmp_ifcs.push_back(ifc);

        pAddr = pAddr->Next;
    }

    PIP_ADAPTER_INFO pInfo = pInfos;
    while (pInfo) {
        for (auto& item : tmp_ifcs) {
            if (strcmp(item.name, pInfo->AdapterName) == 0) {
                // description more friendly
                strncpy(item.name, pInfo->Description, sizeof(item.name));
                strncpy(item.ip, pInfo->IpAddressList.IpAddress.String, sizeof(item.ip));
                strncpy(item.mask, pInfo->IpAddressList.IpMask.String, sizeof(item.mask));
            }
        }

        pInfo = pInfo->Next;
    }

    free(pAddrs);
    free(pInfos);

    // filter
    ifcs.clear();
    for (auto& item : tmp_ifcs) {
        if (strcmp(item.ip, "0.0.0.0") == 0 ||
            strcmp(item.ip, "127.0.0.1") == 0 ||
            strcmp(item.mac, "00:00:00:00:00:00") == 0) {
            continue;
        }
        ifcs.push_back(item);
    }

    return 0;
}
#elif defined(OS_MAC)
#include <ifaddrs.h>
#include <net/if_dl.h>
int ifconfig(std::vector<ifconfig_t>& ifcs) {
    struct ifaddrs *ifas, *ifap;
    int ret = getifaddrs(&ifas);
    if (ret != 0) return ret;
    ifconfig_s tmp;
    for (ifap = ifas; ifap != NULL; ifap = ifap->ifa_next) {
        if (ifap->ifa_addr->sa_family == AF_INET) {
            // ipv4
            struct sockaddr_in* addr = (struct sockaddr_in*)ifap->ifa_addr;
            char* ip = inet_ntoa(addr->sin_addr);
            // filter
            if (strcmp(ip, "0.0.0.0") == 0 || strcmp(ip, "127.0.0.1") == 0) {
                continue;
            }
            memset(&tmp, 0, sizeof(tmp));
            strncpy(tmp.name, ifap->ifa_name, sizeof(tmp.name));
            strncpy(tmp.ip, ip, sizeof(tmp.ip));
            // netmask
            addr = (struct sockaddr_in*)ifap->ifa_netmask;
            char* netmask = inet_ntoa(addr->sin_addr);
            strncpy(tmp.mask, netmask, sizeof(tmp.mask));
            // broadaddr
            addr = (struct sockaddr_in*)ifap->ifa_broadaddr;
            char* broadaddr = inet_ntoa(addr->sin_addr);
            strncpy(tmp.broadcast, broadaddr, sizeof(tmp.broadcast));
            // push_back
            ifcs.push_back(tmp);
        }
    }

    for (ifap = ifas; ifap != NULL; ifap = ifap->ifa_next) {
        if (ifap->ifa_addr->sa_family == AF_LINK) {
            // hwaddr
            for (auto iter = ifcs.begin(); iter != ifcs.end(); ++iter) {
                if (strcmp(iter->name, ifap->ifa_name) == 0) {
                    struct sockaddr_dl* addr = (struct sockaddr_dl*)ifap->ifa_addr;
                    unsigned char* pmac = (unsigned char*)LLADDR(addr);
                    snprintf(iter->mac, sizeof(iter->mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                        pmac[0], pmac[1], pmac[2], pmac[3], pmac[4], pmac[5]);
                    // filter
                    if (strcmp(iter->mac, "00:00:00:00:00:00") == 0) {
                        ifcs.erase(iter);
                    }
                    break;
                }
            }
        }
    }

    freeifaddrs(ifas);
    return 0;
}

#else
int ifconfig(std::vector<ifconfig_t>& ifcs) {
    return -10; // unimplemented
}
#endif

