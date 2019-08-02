#include "hsocket.h"
#include "htime.h"
#include "netinet.h"

int Listen(int port) {
    // socket -> setsockopt -> bind -> listen
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return -sockerrno;
    }
    struct sockaddr_in localaddr;
    socklen_t addrlen = sizeof(localaddr);
    // NOTE: SO_REUSEADDR means that you can reuse sockaddr of TIME_WAIT status
    int reuseaddr = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(int)) < 0) {
        perror("setsockopt");
        goto error;
    }
    memset(&localaddr, 0, addrlen);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localaddr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&localaddr, addrlen) < 0) {
        perror("bind");
        goto error;
    }
    if (listen(listenfd, SOMAXCONN) < 0) {
        perror("listen");
        goto error;
    }
    return listenfd;
error:
    closesocket(listenfd);
    return sockerrno == 0 ? -1 : -sockerrno;
}

int Connect(const char* host, int port, int nonblock) {
    // gethostbyname -> socket -> nonblocking -> connect
    struct sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    memset(&peeraddr, 0, addrlen);
    peeraddr.sin_family = AF_INET;
    inet_pton(peeraddr.sin_family, host, &peeraddr.sin_addr);
    if (peeraddr.sin_addr.s_addr == 0 ||
        peeraddr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* phe = gethostbyname(host);
        if (phe == NULL)    return -h_errno;
        peeraddr.sin_family = phe->h_addrtype;
        memcpy(&peeraddr.sin_addr, phe->h_addr_list[0], phe->h_length);
    }
    peeraddr.sin_port = htons(port);
    int connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        return -sockerrno;
    }
    if (nonblock) {
        nonblocking(connfd);
    }
    int ret = connect(connfd, (struct sockaddr*)&peeraddr, addrlen);
#ifdef OS_WIN
    if (ret < 0 && sockerrno != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && sockerrno != EINPROGRESS) {
#endif
        perror("connect");
        goto error;
    }
    return connfd;
error:
    closesocket(connfd);
    return sockerrno == 0 ? -1 : -sockerrno;
}

#define PING_TIMEOUT    1000 // ms
int Ping(const char* host, int cnt) {
    static uint16_t seq = 0;
    char ip[64] = {0};
    uint64_t start_tick, end_tick;
    uint64_t start_hrtime, end_hrtime;
    int timeout = 0;
    int sendbytes = 64;
    char sendbuf[64];
    char recvbuf[128]; // iphdr + icmp = 84 at least
    struct icmp* icmp_req = (struct icmp*)sendbuf;
    struct iphdr* ipheader = (struct iphdr*)recvbuf;
    struct icmp* icmp_res;
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
    struct sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    memset(&peeraddr, 0, addrlen);
    peeraddr.sin_family = AF_INET;
    inet_pton(peeraddr.sin_family, host, &peeraddr.sin_addr);
    if (peeraddr.sin_addr.s_addr == 0 ||
        peeraddr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* phe = gethostbyname(host);
        if (phe == NULL) {
            printd("unknown host %s\n", host);
            return -h_errno;
        }
        peeraddr.sin_family = phe->h_addrtype;
        memcpy(&peeraddr.sin_addr, phe->h_addr_list[0], phe->h_length);
    }
    inet_ntop(peeraddr.sin_family, &peeraddr.sin_addr, ip, sizeof(ip));
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket");
        if (errno == EPERM) {
            fprintf(stderr, "please use root or sudo to create a raw socket.\n");
        }
        return -sockerrno;
    }

    timeout = PING_TIMEOUT;
    int ret = so_sndtimeo(sockfd, timeout);
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
    icmp_req->icmp_id = getpid();
    for (int i = 0; i < sendbytes - sizeof(struct icmphdr); ++i) {
        icmp_req->icmp_data[i] = i;
    }
    start_tick = gettick();
    while (cnt-- > 0) {
        // NOTE: checksum
        icmp_req->icmp_cksum = 0;
        icmp_req->icmp_seq = ++seq;
        icmp_req->icmp_cksum = checksum((uint8_t*)icmp_req, sendbytes);
        start_hrtime = gethrtime();
        int nsend = sendto(sockfd, sendbuf, sendbytes, 0, (struct sockaddr*)&peeraddr, addrlen);
        if (nsend < 0) {
            perror("sendto");
            continue;
        }
        ++send_cnt;
        addrlen = sizeof(peeraddr);
        int nrecv = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr*)&peeraddr, &addrlen);
        if (nrecv < 0) {
            perror("recvfrom");
            continue;
        }
        ++recv_cnt;
        end_hrtime = gethrtime();
        // check valid
        bool valid = false;
        int iphdr_len = ipheader->ihl * 4;
        int icmp_len = nrecv - iphdr_len;
        if (icmp_len == sendbytes) {
            icmp_res = (struct icmp*)(recvbuf + ipheader->ihl*4);
            if (icmp_res->icmp_type == ICMP_ECHOREPLY &&
                icmp_res->icmp_id == getpid() &&
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
        if (cnt > 0) sleep(1); // sleep a while, then agian
    }
    end_tick = gettick();
    printd("--- %s ping statistics ---\n", host);
    printd("%d packets transmitted, %d received, %d%% packet loss, time %d ms\n",
        send_cnt, recv_cnt, (send_cnt-recv_cnt)*100/(send_cnt==0?1:send_cnt), end_tick-start_tick);
    printd("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
        min_rtt, total_rtt/(ok_cnt==0?1:ok_cnt), max_rtt);

    closesocket(sockfd);
    return ok_cnt;
error:
    closesocket(sockfd);
    return sockerrno == 0 ? -1 : -sockerrno;
}
