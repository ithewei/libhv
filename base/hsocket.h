#ifndef HW_SOCKET_H_
#define HW_SOCKET_H_

#include "hplatform.h"

// socket -> setsockopt -> bind -> listen
// @return sockfd
int Listen(int port);

// gethostbyname -> socket -> nonblocking -> connect
// @return sockfd
int Connect(const char* host, int port, int nonblock = 0);

#ifdef OS_WIN
typedef int socklen_t;
inline int blocking(int sockfd) {
    unsigned long nb = 0;
    return ioctlsocket(sockfd, FIONBIO, &nb);
}
inline int nonblocking(int sockfd) {
    unsigned long nb = 1;
    return ioctlsocket(sockfd, FIONBIO, &nb);
}
#define poll        WSAPoll
#define sockerrno   WSAGetLastError()
#define NIO_EAGAIN  WSAEWOULDBLOCK
#else
#define blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)
#define nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) |  O_NONBLOCK)
#define closesocket close
#define sockerrno   errno
#define NIO_EAGAIN  EAGAIN
#endif

inline int tcp_nodelay(int sockfd, int on = 1) {
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(int));
}

inline int tcp_nopush(int sockfd, int on = 1) {
#ifdef TCP_NOPUSH
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NOPUSH, (const char*)&on, sizeof(int));
#elif defined(TCP_CORK)
    return setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, (const char*)&on, sizeof(int));
#else
    return -10;
#endif
}

inline int tcp_keepalive(int sockfd, int on = 1, int delay = 60) {
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, sizeof(int)) != 0) {
        return sockerrno;
    }

#ifdef TCP_KEEPALIVE
    return setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, (const char*)&delay, sizeof(int));
#elif defined(TCP_KEEPIDLE)
    // TCP_KEEPIDLE     => tcp_keepalive_time
    // TCP_KEEPCNT      => tcp_keepalive_probes
    // TCP_KEEPINTVL    => tcp_keepalive_intvl
    return setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&delay, sizeof(int));
#endif
}

inline int udp_broadcast(int sockfd, int on = 1) {
    return setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(int));
}

#endif // HW_SOCKET_H_
