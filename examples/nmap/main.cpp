#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "nmap.h"
#include "hsocket.h"
#include "hthreadpool.h"

int host_discover_task(std::string segment, void* nmap) {
    Nmap* hosts= (Nmap*)nmap;
    return host_discover(segment.c_str(), hosts);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: nmap segment\n");
        printf("Examples: nmap 192.168.1.123\n");
        printf("          nmap 192.168.1.x/24\n");
        printf("          nmap 192.168.x.x/16\n");
        return -1;
    }

    char* segment = argv[1];
    char* split = strchr(segment, '/');
    int n = 24;
    if (split) {
        *split = '\0';
        n = atoi(split+1);
        if (n != 24 && n != 16) {
            return -2;
        }
    }

    Nmap hosts;
    char ip[INET_ADDRSTRLEN];
    if (n == 24) {
        host_discover(segment, &hosts);
    }
    else if (n == 16) {
        Nmap segs;
        int up_nsegs = segment_discover(segment, &segs);
        if (up_nsegs == 0) return 0;
#if 1
        for (auto& pair : segs) {
            if (pair.second == 1) {
                uint32_t addr = pair.first;
                uint8_t* p = (uint8_t*)&addr;
                // 0,255 reserved
                for (int i = 1; i < 255; ++i) {
                    p[3] = i;
                    hosts[addr] = 0;
                }
            }
        }
        nmap_discover(&hosts);
#else
        if (up_nsegs == 1) {
            for (auto& pair : segs) {
                if (pair.second == 1) {
                    inet_ntop(AF_INET, (void*)&pair.first, ip, sizeof(ip));
                    host_discover(ip, &hosts);
                }
            }
        }
        else {
            // ThreadPool + host_discover
            Nmap* hosts = new Nmap[up_nsegs];
            // use ThreadPool
            HThreadPool tp(4);
            tp.start();
            std::vector<std::future<int>> futures;
            int i = 0;
            for (auto& pair : segs) {
                if (pair.second == 1) {
                    inet_ntop(AF_INET, (void*)&pair.first, ip, sizeof(ip));
                    auto future = tp.commit(host_discover_task, std::string(ip), &hosts[i++]);
                    futures.push_back(std::move(future));
                }
            }
            // wait all task done
            int nhosts = 0;
            for (auto& future : futures) {
                nhosts += future.get();
            }
            // filter up hosts
            std::vector<uint32_t> up_hosts;
            for (int i = 0; i < up_nsegs; ++i) {
                Nmap& nmap = hosts[i];
                for (auto& host : nmap) {
                    if (host.second == 1) {
                        up_hosts.push_back(host.first);
                    }
                }
            }
            delete[] hosts;
        }
#endif
    }

    // filter up hosts
    std::vector<uint32_t> up_hosts;
    for (auto& pair : hosts) {
        if (pair.second == 1) {
            up_hosts.push_back(pair.first);
        }
    }
    // print up hosts
    printf("Up hosts %lu:\n", (unsigned long)up_hosts.size());
    for (auto& host : up_hosts) {
        inet_ntop(AF_INET, (void*)&host, ip, sizeof(ip));
        printf("%s\n", ip);
    }
    return 0;
}
