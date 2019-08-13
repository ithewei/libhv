#include <stdio.h>
#include <stdlib.h>

#include "nmap.h"
#include "hthreadpool.h"

int host_discovery_task(std::string segment, void* nmap) {
    Nmap* hosts= (Nmap*)nmap;
    printf("%p %s------------------------------------------------\n", nmap, segment.c_str());
    return host_discovery(segment.c_str(), hosts);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: cmd segment\n");
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

    if (n == 24) {
        Nmap nmap;
        int ups = host_discovery(segment, &nmap);
        return 0;
    }

    char ip[INET_ADDRSTRLEN];
    if (n == 16) {
        Nmap nmap;
        int up_nsegs = segment_discovery(segment, &nmap);
        if (up_nsegs == 0) return 0;
        if (up_nsegs == 1) {
            for (auto& pair : nmap) {
                if (pair.second == 1) {
                    inet_ntop(AF_INET, (void*)&pair.first, ip, sizeof(ip));
                    Nmap hosts;
                    return host_discovery(ip, &hosts);
                }
            }
        }
        Nmap* hosts = new Nmap[up_nsegs];
        // use ThreadPool
        HThreadPool tp(4);
        tp.start();
        std::vector<std::future<int>> futures;
        int i = 0;
        for (auto& pair : nmap) {
            if (pair.second == 1) {
                inet_ntop(AF_INET, (void*)&pair.first, ip, sizeof(ip));
                auto future = tp.commit(host_discovery_task, std::string(ip), &hosts[i++]);
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
        // print up hosts
        printf("Up hosts %d:\n", nhosts);
        for (auto& host : up_hosts) {
            inet_ntop(AF_INET, (void*)&host, ip, sizeof(ip));
            printf("%s\n", ip);
        }
    }

    return 0;
}
