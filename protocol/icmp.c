#include "icmp.h"

#include "netinet.h"
#include "hdef.h"
#include "hsocket.h"
#include "htime.h"

#define PING_TIMEOUT    1000 // ms
int ping(const char* host, int cnt) {
    static uint16_t seq = 0;
    uint16_t pid16 = (uint16_t)getpid();
    char ip[64] = {0};
    uint32_t start_tick, end_tick;
    uint64_t start_hrtime, end_hrtime;
    int timeout = 0;
    int sendbytes = 64;
    char sendbuf[64];
    char recvbuf[128]; // iphdr + icmp = 84 at least
    icmp_t* icmp_req = (icmp_t*)sendbuf;
    iphdr_t* ipheader = (iphdr_t*)recvbuf;
    icmp_t* icmp_res;
    // ping stat
    int send_cnt = 0;
    int recv_cnt = 0;
    int ok_cnt = 0;
    float rtt, min_rtt, max_rtt, total_rtt;
    rtt = max_rtt = total_rtt = 0.0f;
    min_rtt = 1000000.0f;
    //min_rtt = MIN(rtt, min_rtt);
    //max_rtt = MAX(rtt, max_rtt);
    // gethostbyname -> socket -> setsockopt -> sendto -> recvfrom -> closesocket
    sockaddr_u peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    memset(&peeraddr, 0, addrlen);
    int ret = Resolver(host, &peeraddr);
    if (ret != 0) return ret;
    sockaddr_ip(&peeraddr, ip, sizeof(ip));
    int sockfd = socket(peeraddr.sa.sa_family, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket");
        if (errno == EPERM) {
            fprintf(stderr, "please use root or sudo to create a raw socket.\n");
        }
        return -socket_errno();
    }

    timeout = PING_TIMEOUT;
    ret = so_sndtimeo(sockfd, timeout);
    if (ret < 0) {
        perror("setsockopt");
        goto error;
    }
    timeout = PING_TIMEOUT;
    ret = so_rcvtimeo(sockfd, timeout);
    if (ret < 0) {
        perror("setsockopt");
        goto error;
    }

    icmp_req->icmp_type = ICMP_ECHO;
    icmp_req->icmp_code = 0;
    icmp_req->icmp_id = pid16;
    for (int i = 0; i < sendbytes - sizeof(icmphdr_t); ++i) {
        icmp_req->icmp_data[i] = i;
    }
    start_tick = gettick_ms();
    while (cnt-- > 0) {
        // NOTE: checksum
        icmp_req->icmp_seq = ++seq;
        icmp_req->icmp_cksum = 0;
        icmp_req->icmp_cksum = checksum((uint8_t*)icmp_req, sendbytes);
        start_hrtime = gethrtime_us();
        addrlen = sockaddr_len(&peeraddr);
        int nsend = sendto(sockfd, sendbuf, sendbytes, 0, &peeraddr.sa, addrlen);
        if (nsend < 0) {
            perror("sendto");
            continue;
        }
        ++send_cnt;
        addrlen = sizeof(peeraddr);
        int nrecv = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0, &peeraddr.sa, &addrlen);
        if (nrecv < 0) {
            perror("recvfrom");
            continue;
        }
        ++recv_cnt;
        end_hrtime = gethrtime_us();
        // check valid
        bool valid = false;
        int iphdr_len = ipheader->ihl * 4;
        int icmp_len = nrecv - iphdr_len;
        if (icmp_len == sendbytes) {
            icmp_res = (icmp_t*)(recvbuf + ipheader->ihl*4);
            if (icmp_res->icmp_type == ICMP_ECHOREPLY &&
                icmp_res->icmp_id == pid16 &&
                icmp_res->icmp_seq == seq) {
                valid = true;
            }
        }
        if (valid == false) {
            printd("recv invalid icmp packet!\n");
            continue;
        }
        rtt = (end_hrtime-start_hrtime) / 1000.0f;
        min_rtt = MIN(rtt, min_rtt);
        max_rtt = MAX(rtt, max_rtt);
        total_rtt += rtt;
        printd("%d bytes from %s: icmp_seq=%u ttl=%u time=%.1f ms\n", icmp_len, ip, seq, ipheader->ttl, rtt);
        fflush(stdout);
        ++ok_cnt;
        if (cnt > 0) hv_sleep(1); // sleep a while, then agian
    }
    end_tick = gettick_ms();
    printd("--- %s ping statistics ---\n", host);
    printd("%d packets transmitted, %d received, %d%% packet loss, time %d ms\n",
        send_cnt, recv_cnt, (send_cnt-recv_cnt)*100/(send_cnt==0?1:send_cnt), end_tick-start_tick);
    printd("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
        min_rtt, total_rtt/(ok_cnt==0?1:ok_cnt), max_rtt);

    closesocket(sockfd);
    return ok_cnt;
error:
    closesocket(sockfd);
    return socket_errno() > 0 ? -socket_errno() : -1;
}
