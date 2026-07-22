/*
 * hdns_example — asynchronous DNS resolve demo.
 *
 * Usage: hdns_example [domain] [domain2 ...]
 *   e.g. hdns_example www.example.com github.com localhost 1.1.1.1
 *
 * Resolves each domain asynchronously through the event loop (no blocking
 * getaddrinfo) and prints the resulting addresses. Quits when all queries
 * have completed.
 */

#include "hloop.h"
#include "hdns.h"
#include "hsocket.h"
#include "hbase.h"

static int pending = 0;

static void on_resolved(const hdns_result_t* result, void* userdata) {
    hloop_t* loop = (hloop_t*)userdata;
    if (result->status == HDNS_STATUS_OK) {
        printf("%s =>\n", result->host);
        for (int i = 0; i < result->naddrs; ++i) {
            char ip[SOCKADDR_STRLEN] = {0};
            // cast away const: sockaddr_ip only reads
            sockaddr_ip((sockaddr_u*)&result->addrs[i], ip, sizeof(ip));
            printf("    %s\n", ip);
        }
    } else {
        printf("%s => resolve failed, status=%d\n", result->host, result->status);
    }

    if (--pending == 0) {
        hloop_stop(loop);
    }
}

int main(int argc, char** argv) {
    const char* default_hosts[] = { "localhost", "www.example.com", "github.com" };
    const char** hosts;
    int nhosts;
    if (argc > 1) {
        hosts = (const char**)(argv + 1);
        nhosts = argc - 1;
    } else {
        hosts = default_hosts;
        nhosts = (int)(sizeof(default_hosts) / sizeof(default_hosts[0]));
        printf("usage: %s domain [domain2 ...]\n", argv[0]);
        printf("(no args given, resolving defaults)\n\n");
    }

    hloop_t* loop = hloop_new(0);
    pending = nhosts;
    for (int i = 0; i < nhosts; ++i) {
        hdns_resolve(loop, hosts[i], on_resolved, loop);
    }

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
