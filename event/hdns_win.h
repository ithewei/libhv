#ifndef HV_DNS_WIN_H_
#define HV_DNS_WIN_H_

#include <iphlpapi.h>

static int hdns__win_nameservers(IP_ADAPTER_ADDRESSES* adapters, sockaddr_u* out, int max) {
    static const unsigned char placeholder_prefix[8] = {0xfe, 0xc0, 0, 0, 0, 0, 0xff, 0xff};
    int count = 0;
    if (max <= 0) return 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (IP_ADAPTER_ADDRESSES* ad = adapters; ad; ad = ad->Next) {
            if (ad->OperStatus != IfOperStatusUp || (!!ad->FirstGatewayAddress) != (pass == 0)) continue;
            for (IP_ADAPTER_DNS_SERVER_ADDRESS* dns = ad->FirstDnsServerAddress; dns; dns = dns->Next) {
                struct sockaddr* sa = dns->Address.lpSockaddr;
                sockaddr_u addr;
                memset(&addr, 0, sizeof(addr));
                if (!sa) continue;
                if (sa->sa_family == AF_INET && dns->Address.iSockaddrLength >= (int)sizeof(addr.sin)) {
                    memcpy(&addr.sin, sa, sizeof(addr.sin));
                    addr.sin.sin_port = htons(HDNS_DEFAULT_PORT);
                } else if (sa->sa_family == AF_INET6 && dns->Address.iSockaddrLength >= (int)sizeof(addr.sin6)) {
                    memcpy(&addr.sin6, sa, sizeof(addr.sin6));
                    unsigned char* ip = (unsigned char*)&addr.sin6.sin6_addr;
                    if (memcmp(ip, placeholder_prefix, sizeof(placeholder_prefix)) == 0) continue;
                    if (ip[0] == 0xfe && (ip[1] & 0xc0) == 0x80 && !addr.sin6.sin6_scope_id) {
                        addr.sin6.sin6_scope_id = ad->Ipv6IfIndex;
                        if (!addr.sin6.sin6_scope_id) continue;
                    }
                    addr.sin6.sin6_port = htons(HDNS_DEFAULT_PORT);
                } else {
                    continue;
                }
                int duplicate = 0;
                for (int i = 0; i < count; ++i) {
                    if (sockaddr_compare(&out[i], &addr) == 0 &&
                        (addr.sa.sa_family != AF_INET6 || out[i].sin6.sin6_scope_id == addr.sin6.sin6_scope_id)) {
                        duplicate = 1;
                        break;
                    }
                }
                if (!duplicate) {
                    out[count++] = addr;
                    if (count == max) return count;
                }
            }
        }
    }
    return count;
}

#endif
