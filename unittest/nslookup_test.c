#include <stdio.h>

#include "hplatform.h" // inet_ntop
#include "dns.h" // nslookup

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: nslookup domain [nameserver]\n");
        return -1;
    }

    const char* domain = argv[1];
    const char* nameserver = "127.0.1.1";

#ifdef OS_WIN
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2,2), &wsadata);
#endif

#ifndef OS_LINUX
    nameserver = "114.114.114.114";
    // nameserver = "8.8.8.8";
#endif

    if (argc > 2) {
        nameserver = argv[2];
    }

    uint32_t addrs[16];
    int naddr = nslookup(domain, addrs, 16, nameserver);
    if (naddr < 0) {
        return naddr;
    }
    char ip[16];
    for (int i = 0; i < naddr; ++i) {
        inet_ntop(AF_INET, (void*)&addrs[i], ip, 16);
        printf("%s\n", ip);
    }
    return 0;
}
