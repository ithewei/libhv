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
    char sendbuf[64] = {0};
    char recvbuf[256]; // iphdr + icmp = 84 at least
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
    int ret = ResolveAddr(host, &peeraddr);
    if (ret != 0) return ret;
    sockaddr_ip(&peeraddr, ip, sizeof(ip));
    const bool addr_ipv6 = (peeraddr.sa.sa_family == AF_INET6);
    int sockfd = socket(peeraddr.sa.sa_family, SOCK_RAW, addr_ipv6 ? IPPROTO_ICMPV6 : IPPROTO_ICMP);
#ifdef _WIN32
    if (sockfd == INVALID_SOCKET) {
#else
    if (sockfd <= 0) {
#endif
        perror("socket");
        if (errno == EPERM) {
            fprintf(stderr, "please use root or sudo to create a raw socket.\n");
        }
        return -socket_errno();
    }
    uint8_t ttl = 255;
    switch (peeraddr.sa.sa_family) {
    case AF_INET6:
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (char *)&ttl, sizeof(uint8_t)) < 0) {
            perror("Cannot set socket options!");
            goto error;
        }
        break;
    case AF_INET:
    default:
        if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, (char *)&ttl, sizeof(uint8_t)) < 0) {
            perror("Cannot set socket options!");
            goto error;
        }
    }
    ret = blocking(sockfd);
    if (ret < 0) {
        perror("ioctlsocket blocking");
        goto error;
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

    icmp_req->icmp_type = addr_ipv6 ? ICMPV6_ECHO : ICMP_ECHO;
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
        // ICMP IPv6不传校验码
        if(!addr_ipv6) 
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
        int nrecv;
        bool is_recv_fail = false;
        while (true) {
            memset(recvbuf, 0, sizeof(recvbuf));
            nrecv = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0, &peeraddr.sa, &addrlen);
            if (nrecv < 0) {
                end_hrtime = gethrtime_us();
                rtt = (end_hrtime - start_hrtime) / 1000.0f;
                if(rtt >= PING_TIMEOUT) {
                    is_recv_fail = true;
                    perror("recvfrom");
                    break;
                }
            } else {
                break;
            }
        }
        if(is_recv_fail) continue;
        ++recv_cnt;
        end_hrtime = gethrtime_us();
        // check valid
        bool valid = false;
        // IPv6的ICMP包不含IP头
        int iphdr_len = addr_ipv6 ? 0 : ipheader->ihl * 4;
        int icmp_len = nrecv - iphdr_len;
        if (icmp_len == sendbytes) {
            icmp_res = (icmp_t*)(recvbuf + iphdr_len);
            uint16_t cksum = 0;
            if (!addr_ipv6) {
                //IPv4校验需先把数据包中的校验码段置0
                cksum = icmp_res->icmp_cksum;
                icmp_res->icmp_cksum = 0;
            }
            if ((icmp_res->icmp_type == (addr_ipv6 ? ICMPV6_ECHOREPLY : ICMP_ECHOREPLY)) &&                 
                (addr_ipv6 || (cksum == checksum((uint8_t*)icmp_res, icmp_len))) &&  // IPv6不检验校验码
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
        if (addr_ipv6)
            printd("%d bytes from %s: icmp_seq=%u time=%.1f ms\n", icmp_len, ip, seq, rtt);
        else
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
