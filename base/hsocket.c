#include "hsocket.h"
#include "htime.h"
#include "netinet.h"

#ifdef OS_UNIX
#include <sys/select.h>
#endif

const char* socket_strerror(int err) {
#ifdef OS_WIN
    static char buffer[128];

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_MAX_WIDTH_MASK,
        0, err, 0, buffer, sizeof(buffer), NULL);

    return buffer;
#else
    return strerror(err);
#endif
}

int Resolver(const char* host, sockaddr_un* addr) {
    if (inet_pton(AF_INET, host, &addr->sin.sin_addr) == 1) {
        addr->sa.sa_family = AF_INET; // host is ipv4, so easy ;)
        return 0;
    }

#ifdef ENABLE_IPV6
    if (inet_pton(AF_INET6, host, &addr->sin6.sin6_addr) == 1) {
        addr->sa.sa_family = AF_INET6; // host is ipv6
        return 0;
    }
    struct addrinfo* ais = NULL;
    struct addrinfo hint;
    hint.ai_flags = 0;
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = 0;
    hint.ai_protocol = 0;
    int ret = getaddrinfo(host, NULL, NULL, &ais);
    if (ret != 0 || ais == NULL || ais->ai_addrlen == 0 || ais->ai_addr == NULL) {
        printd("unknown host: %s err:%d:%s\n", host, ret, gai_strerror(ret));
        return ret;
    }
    memcpy(addr, ais->ai_addr, ais->ai_addrlen);
    freeaddrinfo(ais);
#else
    struct hostent* phe = gethostbyname(host);
    if (phe == NULL) {
        printd("unknown host %s err:%d:%s\n", host, h_errno, hstrerror(h_errno));
        return -h_errno;
    }
    addr->sin.sin_family = AF_INET;
    memcpy(&addr->sin.sin_addr, phe->h_addr_list[0], phe->h_length);
#endif
    return 0;
}

int Bind(int port, const char* host, int type) {
    sockaddr_un localaddr;
    socklen_t addrlen = sizeof(localaddr);
    memset(&localaddr, 0, addrlen);
    int ret = sockaddr_assign(&localaddr, host, port);
    if (ret != 0) {
        printf("unknown host: %s\n", host);
        return ret;
    }

    // socket -> setsockopt -> bind
    int sockfd = socket(localaddr.sa.sa_family, type, 0);
    if (sockfd < 0) {
        perror("socket");
        return -socket_errno();
    }

    // NOTE: SO_REUSEADDR means that you can reuse sockaddr of TIME_WAIT status
    int reuseaddr = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(int)) < 0) {
        perror("setsockopt");
        goto error;
    }

    if (bind(sockfd, &localaddr.sa, sockaddrlen(&localaddr)) < 0) {
        perror("bind");
        goto error;
    }

    return sockfd;
error:
    closesocket(sockfd);
    return socket_errno() > 0 ? -socket_errno() : -1;
}

int Listen(int port, const char* host) {
    int sockfd = Bind(port, host, SOCK_STREAM);
    if (sockfd < 0) return sockfd;
    if (listen(sockfd, SOMAXCONN) < 0) {
        perror("listen");
        goto error;
    }
    return sockfd;
error:
    closesocket(sockfd);
    return socket_errno() > 0 ? -socket_errno() : -1;
}

int Connect(const char* host, int port, int nonblock) {
    // Resolver -> socket -> nonblocking -> connect
    sockaddr_un peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    memset(&peeraddr, 0, addrlen);
    int ret = sockaddr_assign(&peeraddr, host, port);
    if (ret != 0) {
        //printf("unknown host: %s\n", host);
        return ret;
    }
    int connfd = socket(peeraddr.sa.sa_family, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        return -socket_errno();
    }
    if (nonblock) {
        nonblocking(connfd);
    }
    ret = connect(connfd, &peeraddr.sa, sockaddrlen(&peeraddr));
#ifdef OS_WIN
    if (ret < 0 && socket_errno() != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && socket_errno() != EINPROGRESS) {
#endif
        perror("connect");
        goto error;
    }
    return connfd;
error:
    closesocket(connfd);
    return socket_errno() > 0 ? -socket_errno() : -1;
}

int ConnectNonblock(const char* host, int port) {
    return Connect(host, port, 1);
}

int ConnectTimeout(const char* host, int port, int ms) {
    int connfd = Connect(host, port, 1);
    if (connfd < 0) return connfd;
    int err;
    socklen_t optlen = sizeof(err);
    struct timeval tv = {ms/1000, (ms%1000)*1000};
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(connfd, &writefds);
    int ret = select(connfd+1, 0, &writefds, 0, &tv);
    if (ret < 0) {
        perror("select");
        goto error;
    }
    if (ret == 0) {
        errno = ETIMEDOUT;
        goto error;
    }
    if (getsockopt(connfd, SOL_SOCKET, SO_ERROR, (char*)&err, &optlen) < 0 || err != 0) {
        goto error;
    }
    blocking(connfd);
    return connfd;
error:
    closesocket(connfd);
    return socket_errno() > 0 ? -socket_errno() : -1;
}

#define PING_TIMEOUT    1000 // ms
int Ping(const char* host, int cnt) {
    static uint16_t seq = 0;
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
    sockaddr_un peeraddr;
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
    icmp_req->icmp_id = getpid();
    for (int i = 0; i < sendbytes - sizeof(icmphdr_t); ++i) {
        icmp_req->icmp_data[i] = i;
    }
    start_tick = gettick();
    while (cnt-- > 0) {
        // NOTE: checksum
        icmp_req->icmp_seq = ++seq;
        icmp_req->icmp_cksum = 0;
        icmp_req->icmp_cksum = checksum((uint8_t*)icmp_req, sendbytes);
        start_hrtime = gethrtime();
        addrlen = sockaddrlen(&peeraddr);
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
        end_hrtime = gethrtime();
        // check valid
        bool valid = false;
        int iphdr_len = ipheader->ihl * 4;
        int icmp_len = nrecv - iphdr_len;
        if (icmp_len == sendbytes) {
            icmp_res = (icmp_t*)(recvbuf + ipheader->ihl*4);
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
    return socket_errno() > 0 ? -socket_errno() : -1;
}
