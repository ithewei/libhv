/*
 * hdns_benchmark — compare blocking getaddrinfo() vs async hdns_resolve().
 *
 * Resolves a list of hostnames two ways and reports wall-clock time:
 *   1. Blocking: sequential getaddrinfo() (what ResolveAddr uses today).
 *   2. Async:    all hostnames resolved concurrently via hdns on one loop.
 *
 * The async path issues all queries up front and lets the single event loop
 * multiplex them, so total time is roughly one slow lookup rather than the
 * sum of all lookups. Crucially, the event loop is never blocked.
 *
 * Usage: hdns_benchmark [host1 host2 ...]
 *
 * NOTE: results depend on your network / DNS cache. Run a couple of times.
 */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "hloop.h"
#include "hdns.h"
#include "hsocket.h"
#include "htime.h"

static const char* default_hosts[] = {
    "www.example.com", "github.com", "www.google.com", "www.cloudflare.com",
    "www.wikipedia.org", "www.baidu.com", "www.qq.com", "www.taobao.com",
};

static int g_pending = 0;
static int g_ok = 0;

static void on_resolved(const hdns_result_t* result, void* userdata) {
    hloop_t* loop = (hloop_t*)userdata;
    if (result->status == HDNS_STATUS_OK) ++g_ok;
    if (--g_pending == 0) hloop_stop(loop);
}

int main(int argc, char** argv) {
    const char** hosts;
    int nhosts;
    if (argc > 1) {
        hosts = (const char**)(argv + 1);
        nhosts = argc - 1;
    } else {
        hosts = default_hosts;
        nhosts = (int)(sizeof(default_hosts) / sizeof(default_hosts[0]));
    }

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    printf("Resolving %d hostnames.\n\n", nhosts);

    // -------- 1. blocking getaddrinfo (sequential) --------
    unsigned long long t0 = gethrtime_us();
    int blk_ok = 0;
    for (int i = 0; i < nhosts; ++i) {
        struct addrinfo* ai = NULL;
        int ret = getaddrinfo(hosts[i], NULL, NULL, &ai);
        if (ret == 0 && ai) { ++blk_ok; freeaddrinfo(ai); }
    }
    unsigned long long t1 = gethrtime_us();
    double blk_ms = (t1 - t0) / 1000.0;
    printf("[blocking getaddrinfo] %d/%d ok in %.1f ms (sequential)\n",
           blk_ok, nhosts, blk_ms);

    // -------- 2. async hdns (concurrent on one loop) --------
    hloop_t* loop = hloop_new(0);
    g_pending = nhosts;
    g_ok = 0;
    unsigned long long a0 = gethrtime_us();
    for (int i = 0; i < nhosts; ++i) {
        // disable cache so the comparison is apples-to-apples (real queries)
        hdns_options_t opt;
        memset(&opt, 0, sizeof(opt));
        opt.family = HDNS_QUERY_A;
        opt.timeout_ms = 5000;
        opt.retries = 2;
        opt.use_cache = 0;
        hdns_resolve_ex(loop, hosts[i], &opt, on_resolved, loop);
    }
    hloop_run(loop);
    unsigned long long a1 = gethrtime_us();
    double async_ms = (a1 - a0) / 1000.0;
    printf("[async hdns]           %d/%d ok in %.1f ms (concurrent)\n",
           g_ok, nhosts, async_ms);

    printf("\nspeedup: %.2fx (async vs sequential blocking)\n",
           async_ms > 0 ? blk_ms / async_ms : 0.0);
    printf("NOTE: the real win is that the event loop is never blocked during\n"
           "      async resolution, so other IO/timers keep running.\n");

    hloop_free(&loop);
    return 0;
}
