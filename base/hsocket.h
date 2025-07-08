#ifndef HV_SOCKET_H_
#define HV_SOCKET_H_

#include "hexport.h"
#include "hplatform.h"

#ifdef ENABLE_UDS
#ifdef OS_WIN
    #include <afunix.h> // import struct sockaddr_un
#else
    #include <sys/un.h> // import struct sockaddr_un
#endif
#endif

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

#define LOCALHOST   "127.0.0.1"
#define ANYADDR     "0.0.0.0"

BEGIN_EXTERN_C

HV_INLINE int socket_errno() {
#ifdef OS_WIN
    return WSAGetLastError();
#else
    return errno;
#endif
}
HV_EXPORT const char* socket_strerror(int err);

#ifdef OS_WIN

typedef SOCKET  hsocket_t;
typedef int     socklen_t;

void WSAInit();
void WSADeinit();

HV_INLINE int blocking(int sockfd) {
    unsigned long nb = 0;
    return ioctlsocket(sockfd, FIONBIO, &nb);
}
HV_INLINE int nonblocking(int sockfd) {
    unsigned long nb = 1;
    return ioctlsocket(sockfd, FIONBIO, &nb);
}

#undef  EAGAIN
#define EAGAIN      WSAEWOULDBLOCK

#undef  EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS

#undef  EINTR
#define EINTR       WSAEINTR

#undef  ENOTSOCK
#define ENOTSOCK    WSAENOTSOCK

#undef  EMSGSIZE
#define EMSGSIZE    WSAEMSGSIZE

#else

typedef int     hsocket_t;

#ifndef SOCKET
#define SOCKET int
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET  -1
#endif

HV_INLINE int blocking(int s) {
    return fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
}

HV_INLINE int nonblocking(int s) {
    return fcntl(s, F_SETFL, fcntl(s, F_GETFL) |  O_NONBLOCK);
}

#ifndef closesocket
HV_INLINE int closesocket(int sockfd) {
    return close(sockfd);
}
#endif

#endif

#ifndef SAFE_CLOSESOCKET
#define SAFE_CLOSESOCKET(fd)  do {if ((fd) >= 0) {closesocket(fd); (fd) = -1;}} while(0)
#endif

//-----------------------------sockaddr_u----------------------------------------------
typedef union {
    struct sockaddr     sa;
    struct sockaddr_in  sin;
    struct sockaddr_in6 sin6;
#ifdef ENABLE_UDS
    struct sockaddr_un  sun;
#endif
} sockaddr_u;

HV_EXPORT bool is_ipv4(const char* host);
HV_EXPORT bool is_ipv6(const char* host);
HV_INLINE bool is_ipaddr(const char* host) {
    return is_ipv4(host) || is_ipv6(host);
}

// @param host: domain or ip
// @retval 0:succeed
HV_EXPORT int ResolveAddr(const char* host, sockaddr_u* addr);

HV_EXPORT const char* sockaddr_ip(sockaddr_u* addr, char *ip, int len);
HV_EXPORT uint16_t sockaddr_port(sockaddr_u* addr);
HV_EXPORT int sockaddr_set_ip(sockaddr_u* addr, const char* host);
HV_EXPORT void sockaddr_set_port(sockaddr_u* addr, int port);
HV_EXPORT int sockaddr_set_ipport(sockaddr_u* addr, const char* host, int port);
HV_EXPORT socklen_t sockaddr_len(sockaddr_u* addr);
HV_EXPORT const char* sockaddr_str(sockaddr_u* addr, char* buf, int len);
HV_EXPORT int sockaddr_compare(const sockaddr_u* addr1, const sockaddr_u* addr2);

//#define INET_ADDRSTRLEN   16
//#define INET6_ADDRSTRLEN  46
#ifdef ENABLE_UDS
#define SOCKADDR_STRLEN     sizeof(((struct sockaddr_un*)(NULL))->sun_path)
HV_INLINE void sockaddr_set_path(sockaddr_u* addr, const char* path) {
    addr->sa.sa_family = AF_UNIX;
    strncpy(addr->sun.sun_path, path, sizeof(addr->sun.sun_path));
}
#else
#define SOCKADDR_STRLEN     64 // ipv4:port | [ipv6]:port
#endif

HV_INLINE void sockaddr_print(sockaddr_u* addr) {
    char buf[SOCKADDR_STRLEN] = {0};
    sockaddr_str(addr, buf, sizeof(buf));
    puts(buf);
}

#define SOCKADDR_LEN(addr)      sockaddr_len((sockaddr_u*)addr)
#define SOCKADDR_STR(addr, buf) sockaddr_str((sockaddr_u*)addr, buf, sizeof(buf))
#define SOCKADDR_PRINT(addr)    sockaddr_print((sockaddr_u*)addr)
//=====================================================================================

// socket -> setsockopt -> bind
// @param type: SOCK_STREAM(tcp) SOCK_DGRAM(udp)
// @return sockfd
HV_EXPORT int Bind(int port, const char* host DEFAULT(ANYADDR), int type DEFAULT(SOCK_STREAM));

// Bind -> listen
// @return listenfd
HV_EXPORT int Listen(int port, const char* host DEFAULT(ANYADDR));

// @return connfd
// ResolveAddr -> socket -> nonblocking -> connect
HV_EXPORT int Connect(const char* host, int port, int nonblock DEFAULT(0));
// Connect(host, port, 1)
HV_EXPORT int ConnectNonblock(const char* host, int port);
// Connect(host, port, 1) -> select -> blocking
#define DEFAULT_CONNECT_TIMEOUT 10000 // ms
HV_EXPORT int ConnectTimeout(const char* host, int port, int ms DEFAULT(DEFAULT_CONNECT_TIMEOUT));

#ifdef ENABLE_UDS
HV_EXPORT int BindUnix(const char* path, int type DEFAULT(SOCK_STREAM));
HV_EXPORT int ListenUnix(const char* path);
HV_EXPORT int ConnectUnix(const char* path, int nonblock DEFAULT(0));
HV_EXPORT int ConnectUnixNonblock(const char* path);
HV_EXPORT int ConnectUnixTimeout(const char* path, int ms DEFAULT(DEFAULT_CONNECT_TIMEOUT));
#endif

// Just implement Socketpair(AF_INET, SOCK_STREAM, 0, sv);
HV_EXPORT int Socketpair(int family, int type, int protocol, int sv[2]);

HV_INLINE int tcp_nodelay(int sockfd, int on DEFAULT(1)) {
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(int));
}

HV_INLINE int tcp_nopush(int sockfd, int on DEFAULT(1)) {
#ifdef TCP_NOPUSH
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NOPUSH, (const char*)&on, sizeof(int));
#elif defined(TCP_CORK)
    return setsockopt(sockfd, IPPROTO_TCP, TCP_CORK, (const char*)&on, sizeof(int));
#else
    return 0;
#endif
}

HV_INLINE int tcp_keepalive(int sockfd, int on DEFAULT(1), int delay DEFAULT(60)) {
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

HV_INLINE int udp_broadcast(int sockfd, int on DEFAULT(1)) {
    return setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(int));
}

HV_INLINE int ip_v6only(int sockfd, int on DEFAULT(1)) {
#ifdef IPV6_V6ONLY
    return setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(int));
#else
    return 0;
#endif
}

// send timeout
HV_INLINE int so_sndtimeo(int sockfd, int timeout) {
#ifdef OS_WIN
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(int));
#else
    struct timeval tv = {timeout/1000, (timeout%1000)*1000};
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

// recv timeout
HV_INLINE int so_rcvtimeo(int sockfd, int timeout) {
#ifdef OS_WIN
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(int));
#else
    struct timeval tv = {timeout/1000, (timeout%1000)*1000};
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// send buffer size
HV_INLINE int so_sndbuf(int sockfd, int len) {
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&len, sizeof(int));
}

// recv buffer size
HV_INLINE int so_rcvbuf(int sockfd, int len) {
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&len, sizeof(int));
}

HV_INLINE int so_reuseaddr(int sockfd, int on DEFAULT(1)) {
#ifdef SO_REUSEADDR
    // NOTE: SO_REUSEADDR allow to reuse sockaddr of TIME_WAIT status
    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(int));
#else
    return 0;
#endif
}

HV_INLINE int so_reuseport(int sockfd, int on DEFAULT(1)) {
#ifdef SO_REUSEPORT
    // NOTE: SO_REUSEPORT allow multiple sockets to bind same port
    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (const char*)&on, sizeof(int));
#else
    return 0;
#endif
}

HV_INLINE int so_linger(int sockfd, int timeout DEFAULT(1)) {
#ifdef SO_LINGER
    struct linger linger;
    if (timeout >= 0) {
        linger.l_onoff = 1;
        linger.l_linger = timeout;
    } else {
        linger.l_onoff = 0;
        linger.l_linger = 0;
    }
    // NOTE: SO_LINGER change the default behavior of close, send RST, avoid TIME_WAIT
    return setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (const char*)&linger, sizeof(linger));
#else
    return 0;
#endif
}

END_EXTERN_C

#endif // HV_SOCKET_H_
