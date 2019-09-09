#ifndef HW_SOCKET_H_
#define HW_SOCKET_H_

#include "hplatform.h"
#include "hdef.h"
#include "hbase.h"

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

BEGIN_EXTERN_C

static inline int socket_errno() {
#ifdef OS_WIN
    return WSAGetLastError();
#else
    return errno;
#endif
}
char* socket_strerror(int err);

// socket -> setsockopt -> bind
// @param type: SOCK_STREAM(tcp) SOCK_DGRAM(udp)
// @return sockfd
int Bind(int port, int type DEFAULT(SOCK_STREAM));

// Bind -> listen
// @return sockfd
int Listen(int port);

// @param host: domain or ip
// @retval 0:succeed
int Resolver(const char* host, struct sockaddr* addr);

// @return sockfd
// Resolver -> socket -> nonblocking -> connect
int Connect(const char* host, int port, int nonblock DEFAULT(0));
// Connect(host, port, 1)
int ConnectNonblock(const char* host, int port);
// Connect(host, port, 1) -> select -> blocking
int ConnectTimeout(const char* host, int port, int ms);

// @param cnt: ping count
// @return: ok count
// @note: printd $CC -DPRINT_DEBUG
int Ping(const char* host, int cnt DEFAULT(4));

#ifdef OS_WIN
typedef int socklen_t;
static inline int blocking(int sockfd) {
    unsigned long nb = 0;
    return ioctlsocket(sockfd, FIONBIO, &nb);
}
static inline int nonblocking(int sockfd) {
    unsigned long nb = 1;
    return ioctlsocket(sockfd, FIONBIO, &nb);
}
#define poll        WSAPoll
#undef  EAGAIN
#define EAGAIN      WSAEWOULDBLOCK
#undef  EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#undef  ENOTSOCK
#define ENOTSOCK    WSAENOTSOCK
#else
#define blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)
#define nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) |  O_NONBLOCK)
typedef int         SOCKET;
#define INVALID_SOCKET  -1
#define closesocket close
#endif

static inline const char* sockaddr_ntop(const struct sockaddr* addr, char *ip, int len) {
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        return inet_ntop(AF_INET, &sin->sin_addr, ip, len);
    }
    else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        return inet_ntop(AF_INET6, &sin6->sin6_addr, ip, len);
    }
    return ip;
}

static inline uint16_t sockaddr_htons(const struct sockaddr* addr) {
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        return htons(sin->sin_port);
    }
    else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        return htons(sin6->sin6_port);
    }
    return 0;
}

static inline void sockaddr_printf(const struct sockaddr* addr) {
    char ip[INET6_ADDRSTRLEN] = {0};
    int port = 0;
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        port = htons(sin->sin_port);
    }
    else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
        port = htons(sin6->sin6_port);
    }
    printf("%s:%d\n", ip, port);
}

static inline const char* sockaddr_snprintf(const struct sockaddr* addr, char* buf, int len) {
    int port = 0;
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &sin->sin_addr, buf, len);
        port = htons(sin->sin_port);
    }
    else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, buf, len);
        port = htons(sin6->sin6_port);
    }
    char sport[16] = {0};
    snprintf(sport, sizeof(sport), ":%d", port);
    safe_strncat(buf, sport, len);
    return buf;
}

static inline int tcp_nodelay(int sockfd, int on DEFAULT(1)) {
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(int));
}

static inline int tcp_nopush(int sockfd, int on DEFAULT(1)) {
#ifdef TCP_NOPUSH
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NOPUSH, (const char*)&on, sizeof(int));
#elif defined(TCP_CORK)
    return setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, (const char*)&on, sizeof(int));
#else
    return -10;
#endif
}

static inline int tcp_keepalive(int sockfd, int on DEFAULT(1), int delay DEFAULT(60)) {
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, sizeof(int)) != 0) {
        return socket_errno();
    }

#ifdef TCP_KEEPALIVE
    return setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, (const char*)&delay, sizeof(int));
#elif defined(TCP_KEEPIDLE)
    // TCP_KEEPIDLE     => tcp_keepalive_time
    // TCP_KEEPCNT      => tcp_keepalive_probes
    // TCP_KEEPINTVL    => tcp_keepalive_intvl
    return setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&delay, sizeof(int));
#else
    return 0;
#endif
}

static inline int udp_broadcast(int sockfd, int on DEFAULT(1)) {
    return setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(int));
}

// send timeout
static inline int so_sndtimeo(int sockfd, int timeout) {
#ifdef OS_WIN
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(int));
#else
    struct timeval tv = {timeout/1000, (timeout%1000)*1000};
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

// recv timeout
static inline int so_rcvtimeo(int sockfd, int timeout) {
#ifdef OS_WIN
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(int));
#else
    struct timeval tv = {timeout/1000, (timeout%1000)*1000};
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

END_EXTERN_C

#endif // HW_SOCKET_H_
