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

// Some platforms spell the IPv6 group options IPV6_JOIN_GROUP/IPV6_LEAVE_GROUP,
// others IPV6_ADD_MEMBERSHIP/IPV6_DROP_MEMBERSHIP; they are the same option.
#if !defined(IPV6_ADD_MEMBERSHIP) && defined(IPV6_JOIN_GROUP)
#define IPV6_ADD_MEMBERSHIP     IPV6_JOIN_GROUP
#endif
#if !defined(IPV6_DROP_MEMBERSHIP) && defined(IPV6_LEAVE_GROUP)
#define IPV6_DROP_MEMBERSHIP    IPV6_LEAVE_GROUP
#endif

// Group join/leave use struct ip_mreq / ipv6_mreq. On glibc struct ip_mreq is
// gated behind __USE_MISC, which strict ISO C (__STRICT_ANSI__, e.g. -std=c99
// without _GNU_SOURCE) turns off, so the struct is not visible there -- a
// condition that cannot be expressed with #ifdef. HV_HAVE_MULTICAST captures
// exactly that case so the group helpers compile out instead of breaking every
// translation unit that merely includes this header. Everywhere else
// (macOS/BSD/Windows/Android, musl, any C++ or -std=gnu99 build such as how
// libhv compiles hsocket.c on Linux) they are available. The scalar sender
// options further below do not use these structs and are guarded per-function
// with #ifdef, like ip_v6only().
#if !(defined(__GLIBC__) && defined(__STRICT_ANSI__) && !defined(__USE_MISC))
#define HV_HAVE_MULTICAST 1
#else
#define HV_HAVE_MULTICAST 0
#endif

#if HV_HAVE_MULTICAST

// UDP multicast: join/leave a multicast group.
// v4: local_host is the local interface IPv4 address (NULL/"0.0.0.0" = any).
// v6: ifindex is the interface index (0 = default), see if_nametoindex().
HV_INLINE int udp_multicast_join4(int sockfd, const char* group, const char* local_host DEFAULT(NULL)) {
#ifdef IP_ADD_MEMBERSHIP
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(group);
    mreq.imr_interface.s_addr = (local_host && *local_host) ? inet_addr(local_host) : htonl(INADDR_ANY);
    return setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
#else
    (void)sockfd; (void)group; (void)local_host; return -1;
#endif
}
HV_INLINE int udp_multicast_leave4(int sockfd, const char* group, const char* local_host DEFAULT(NULL)) {
#ifdef IP_DROP_MEMBERSHIP
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(group);
    mreq.imr_interface.s_addr = (local_host && *local_host) ? inet_addr(local_host) : htonl(INADDR_ANY);
    return setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
#else
    (void)sockfd; (void)group; (void)local_host; return -1;
#endif
}
HV_INLINE int udp_multicast_join6(int sockfd, const char* group, unsigned int ifindex DEFAULT(0)) {
#ifdef IPV6_ADD_MEMBERSHIP
    struct ipv6_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    if (inet_pton(AF_INET6, group, &mreq.ipv6mr_multiaddr) != 1) return -1;
    mreq.ipv6mr_interface = ifindex;
    return setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
#else
    (void)sockfd; (void)group; (void)ifindex; return -1;
#endif
}
HV_INLINE int udp_multicast_leave6(int sockfd, const char* group, unsigned int ifindex DEFAULT(0)) {
#ifdef IPV6_DROP_MEMBERSHIP
    struct ipv6_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    if (inet_pton(AF_INET6, group, &mreq.ipv6mr_multiaddr) != 1) return -1;
    mreq.ipv6mr_interface = ifindex;
    return setsockopt(sockfd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
#else
    (void)sockfd; (void)group; (void)ifindex; return -1;
#endif
}

// Family-agnostic multicast join/leave: dispatches to the v4/v6 variant by
// detecting an IPv6 group address (contains ':'). For IPv4 groups, local_iface
// is the local interface IPv4 address (NULL/"0.0.0.0" = any); it is ignored for
// IPv6 groups (which use the default interface, ifindex 0).
HV_INLINE int udp_multicast_join(int sockfd, const char* group, const char* local_iface DEFAULT(NULL)) {
    if (group == NULL) return -1;
    if (strchr(group, ':')) return udp_multicast_join6(sockfd, group, 0);
    return udp_multicast_join4(sockfd, group, local_iface);
}
HV_INLINE int udp_multicast_leave(int sockfd, const char* group, const char* local_iface DEFAULT(NULL)) {
    if (group == NULL) return -1;
    if (strchr(group, ':')) return udp_multicast_leave6(sockfd, group, 0);
    return udp_multicast_leave4(sockfd, group, local_iface);
}

// UDP multicast sender options. Each is guarded with #ifdef and returns -1 when
// the option is unavailable on the platform.
// _if:   choose the local egress interface. v4 takes the interface IPv4 address
//        string; v6 takes the interface index (see if_nametoindex()).
// _ttl:  multicast hop limit (1 = same subnet, OS default is 1).
// _loop: whether the sender also receives its own datagrams (on = 1, off = 0).
// The generic udp_multicast_set_ttl/loop pick v4 or v6 by the socket's family.
HV_INLINE int udp_multicast_set_if4(int sockfd, const char* local_iface) {
#ifdef IP_MULTICAST_IF
    struct in_addr addr;
    memset(&addr, 0, sizeof(addr));
    addr.s_addr = (local_iface && *local_iface) ? inet_addr(local_iface) : htonl(INADDR_ANY);
    return setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&addr, sizeof(addr));
#else
    (void)sockfd; (void)local_iface; return -1;
#endif
}
HV_INLINE int udp_multicast_set_ttl4(int sockfd, int ttl) {
#ifdef IP_MULTICAST_TTL
#ifdef OS_WIN
    // Winsock wants a DWORD-sized optval for IP_MULTICAST_TTL.
    int v = ttl;
#else
    unsigned char v = (unsigned char)ttl;
#endif
    return setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&v, sizeof(v));
#else
    (void)sockfd; (void)ttl; return -1;
#endif
}
HV_INLINE int udp_multicast_set_loop4(int sockfd, int on DEFAULT(1)) {
#ifdef IP_MULTICAST_LOOP
#ifdef OS_WIN
    int v = on ? 1 : 0;
#else
    unsigned char v = on ? 1 : 0;
#endif
    return setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&v, sizeof(v));
#else
    (void)sockfd; (void)on; return -1;
#endif
}
HV_INLINE int udp_multicast_set_if6(int sockfd, unsigned int ifindex) {
#ifdef IPV6_MULTICAST_IF
    return setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&ifindex, sizeof(ifindex));
#else
    (void)sockfd; (void)ifindex; return -1;
#endif
}
HV_INLINE int udp_multicast_set_ttl6(int sockfd, int hops) {
#ifdef IPV6_MULTICAST_HOPS
    // IPv6 multicast options take an int optval.
    return setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)&hops, sizeof(hops));
#else
    (void)sockfd; (void)hops; return -1;
#endif
}
HV_INLINE int udp_multicast_set_loop6(int sockfd, int on DEFAULT(1)) {
#ifdef IPV6_MULTICAST_LOOP
    int v = on ? 1 : 0;
    return setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)&v, sizeof(v));
#else
    (void)sockfd; (void)on; return -1;
#endif
}
// Generic ttl/loop: pick v4 or v6 by the socket's address family.
HV_INLINE int udp_multicast_set_ttl(int sockfd, int ttl) {
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (getsockname(sockfd, (struct sockaddr*)&ss, &len) != 0) return -1;
    if (ss.ss_family == AF_INET6) return udp_multicast_set_ttl6(sockfd, ttl);
    return udp_multicast_set_ttl4(sockfd, ttl);
}
HV_INLINE int udp_multicast_set_loop(int sockfd, int on DEFAULT(1)) {
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (getsockname(sockfd, (struct sockaddr*)&ss, &len) != 0) return -1;
    if (ss.ss_family == AF_INET6) return udp_multicast_set_loop6(sockfd, on);
    return udp_multicast_set_loop4(sockfd, on);
}

#endif // HV_HAVE_MULTICAST

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
