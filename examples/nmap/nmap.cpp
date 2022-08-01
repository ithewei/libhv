#include "nmap.h"
#include "hloop.h"
#include "hstring.h"
#include "hsocket.h"
#include "netinet.h"

using namespace hv;

#define MAX_RECVFROM_TIMEOUT    5000 // ms
#define MAX_SENDTO_PERSOCKET    1024

typedef struct nmap_ctx_s {
    Nmap*   nmap;
    int     send_cnt;
    int     recv_cnt;
    int     up_cnt;
    int     idle_cnt;
} nmap_ctx_t;

static void on_idle(hidle_t* idle) {
    hloop_t* loop = hevent_loop(idle);
    nmap_ctx_t* ctx = (nmap_ctx_t*)hloop_userdata(loop);
    ctx->idle_cnt++;
    if (ctx->idle_cnt == 1) {
        // try again?
    }
    hloop_stop(loop);
}

static void on_timer(htimer_t* timer) {
    hloop_t* loop = hevent_loop(timer);
    hloop_stop(loop);
}

static void on_recvfrom(hio_t* io, void* buf, int readbytes) {
    //printd("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    /*
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printd("[%s] <=> [%s]\n",
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    */
    hloop_t* loop = hevent_loop(io);
    nmap_ctx_t* ctx = (nmap_ctx_t*)hloop_userdata(loop);
    if (++ctx->recv_cnt == ctx->send_cnt) {
        //hloop_stop(loop);
    }
    Nmap* nmap = ctx->nmap;
    struct sockaddr_in* peeraddr = (struct sockaddr_in*)hio_peeraddr(io);
    auto iter = nmap->find(peeraddr->sin_addr.s_addr);
    if (iter != nmap->end()) {
        if (iter->second == 0) {
            iter->second = 1;
            if (++ctx->up_cnt == nmap->size()) {
                hloop_stop(loop);
            }
        }
    }
}

int nmap_discover(Nmap* nmap) {
    hloop_t* loop = hloop_new(0);
    uint64_t start_hrtime = hloop_now_hrtime(loop);

    nmap_ctx_t ctx;
    ctx.nmap = nmap;
    ctx.send_cnt = 0;
    ctx.recv_cnt = 0;
    ctx.up_cnt = 0;
    ctx.idle_cnt = 0;
    hloop_set_userdata(loop, &ctx);

    char recvbuf[128];
    // icmp
    char sendbuf[44]; // 20IP + 44ICMP = 64
    icmp_t* icmp_req = (icmp_t*)sendbuf;
    icmp_req->icmp_type = ICMP_ECHO;
    icmp_req->icmp_code = 0;
    icmp_req->icmp_id = getpid();
    for (int i = 0; i < sizeof(sendbuf) - sizeof(icmphdr_t); ++i) {
        icmp_req->icmp_data[i] = i;
    }
    struct sockaddr_in peeraddr;
    hio_t* io = NULL;
    for (auto iter = nmap->begin(); iter != nmap->end(); ++iter) {
        if (iter->second == 1) continue;
        if (ctx.send_cnt % MAX_SENDTO_PERSOCKET == 0) {
            // socket
            int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
            if (sockfd < 0) {
                perror("socket");
                if (errno == EPERM) {
                    fprintf(stderr, "please use root or sudo to create a raw socket.\n");
                }
                return -socket_errno();
            }
            nonblocking(sockfd);
            so_sndbuf(sockfd, 425984); // 416K

            io = hio_get(loop, sockfd);
            if (io == NULL) return -1;
            hio_set_type(io, HIO_TYPE_IP);
            struct sockaddr_in localaddr;
            socklen_t addrlen = sizeof(localaddr);
            memset(&localaddr, 0, addrlen);
            localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
            hio_set_localaddr(io, (struct sockaddr*)&localaddr, addrlen);
            hrecvfrom(loop, sockfd, recvbuf, sizeof(recvbuf), on_recvfrom);
        }
        icmp_req->icmp_seq = iter->first;
        icmp_req->icmp_cksum = 0;
        icmp_req->icmp_cksum = checksum((uint8_t*)icmp_req, sizeof(sendbuf));
        socklen_t addrlen = sizeof(peeraddr);
        memset(&peeraddr, 0, addrlen);
        peeraddr.sin_family = AF_INET;
        peeraddr.sin_addr.s_addr = iter->first;
        hio_set_peeraddr(io, (struct sockaddr*)&peeraddr, addrlen);
        hsendto(loop, hio_fd(io), sendbuf, sizeof(sendbuf), NULL);
        ++ctx.send_cnt;
    }

    htimer_add(loop, on_timer, MAX_RECVFROM_TIMEOUT, 1);
    hidle_add(loop, on_idle, 3);

    hloop_run(loop);
    uint64_t end_hrtime = hloop_now_hrtime(loop);
    hloop_free(&loop);

    // print result
    char ip[INET_ADDRSTRLEN];
    auto iter = nmap->begin();
    while (iter != nmap->end()) {
        inet_ntop(AF_INET, (void*)&iter->first, ip, sizeof(ip));
        printd("%s\t is %s.\n", ip, iter->second == 0 ? "down" : "up");
        ++iter;
    }
    printd("Nmap done: %lu IP addresses (%d hosts up) scanned in %.2f seconds\n",
            (unsigned long)nmap->size(), ctx.up_cnt, (end_hrtime-start_hrtime)/1000000.0f);

    return ctx.up_cnt;
}

int segment_discover(const char* segment16, Nmap* nmap) {
    StringList strlist = split(segment16, '.');
    if (strlist.size() != 4) return -1;
    uint32_t addr = 0;
    uint8_t* p = (uint8_t*)&addr;
    p[0] = atoi(strlist[0].c_str());
    p[1] = atoi(strlist[1].c_str());
    p[3] = 1;
    printd("Nmap scan %u.%u.x.1...\n", p[0], p[1]);
    nmap->clear();
    for (int i = 0; i < 256; ++i) {
        p[2] = i;
        (*nmap)[addr] = 0;
    }
    return nmap_discover(nmap);
}

int host_discover(const char* segment24, Nmap* nmap) {
    StringList strlist = split(segment24, '.');
    if (strlist.size() != 4) return -1;
    uint32_t addr = 0;
    uint8_t* p = (uint8_t*)&addr;
    p[0] = atoi(strlist[0].c_str());
    p[1] = atoi(strlist[1].c_str());
    p[2] = atoi(strlist[2].c_str());
    printd("Nmap scan %u.%u.%u.x...\n", p[0], p[1], p[2]);
    // 0,255 reserved
    nmap->clear();
    for (int i = 1; i < 255; ++i) {
        p[3] = i;
        (*nmap)[addr] = 0;
    }
    return nmap_discover(nmap);
}
