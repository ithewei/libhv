#include "hsocket.h"

#include "hdef.h"

#ifdef OS_WIN
static int s_wsa_initialized = 0;
#endif

static inline int socket_errno_negative() {
    int err = socket_errno();
    return err > 0 ? -err : -1;
}

const char* socket_strerror(int err) {
#ifdef OS_WIN
    static char buffer[128];

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_MAX_WIDTH_MASK,
        0, ABS(err), 0, buffer, sizeof(buffer), NULL);

    return buffer;
#else
    return strerror(ABS(err));
#endif
}

int Resolver(const char* host, sockaddr_u* addr) {
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
        printd("unknown host %s err:%d\n", host, h_errno);
        return -h_errno;
    }
    addr->sin.sin_family = AF_INET;
    memcpy(&addr->sin.sin_addr, phe->h_addr_list[0], phe->h_length);
#endif
    return 0;
}

const char* sockaddr_ip(sockaddr_u* addr, char *ip, int len) {
    if (addr->sa.sa_family == AF_INET) {
        return inet_ntop(AF_INET, &addr->sin.sin_addr, ip, len);
    }
    else if (addr->sa.sa_family == AF_INET6) {
        return inet_ntop(AF_INET6, &addr->sin6.sin6_addr, ip, len);
    }
    return ip;
}

uint16_t sockaddr_port(sockaddr_u* addr) {
    uint16_t port = 0;
    if (addr->sa.sa_family == AF_INET) {
        port = ntohs(addr->sin.sin_port);
    }
    else if (addr->sa.sa_family == AF_INET6) {
        port = ntohs(addr->sin6.sin6_port);
    }
    return port;
}

int sockaddr_set_ip(sockaddr_u* addr, const char* host) {
    if (!host || *host == '\0') {
        addr->sin.sin_family = AF_INET;
        addr->sin.sin_addr.s_addr = htonl(INADDR_ANY);
        return 0;
    }
    return Resolver(host, addr);
}

void sockaddr_set_port(sockaddr_u* addr, int port) {
    if (addr->sa.sa_family == AF_INET) {
        addr->sin.sin_port = htons(port);
    }
    else if (addr->sa.sa_family == AF_INET6) {
        addr->sin6.sin6_port = htons(port);
    }
}

int sockaddr_set_ipport(sockaddr_u* addr, const char* host, int port) {
    int ret = sockaddr_set_ip(addr, host);
    if (ret != 0) return ret;
    sockaddr_set_port(addr, port);
    // SOCKADDR_PRINT(addr);
    return 0;
}

socklen_t sockaddr_len(sockaddr_u* addr) {
    if (addr->sa.sa_family == AF_INET) {
        return sizeof(struct sockaddr_in);
    }
    else if (addr->sa.sa_family == AF_INET6) {
        return sizeof(struct sockaddr_in6);
    }
#ifdef ENABLE_UDS
    else if (addr->sa.sa_family == AF_UNIX) {
        return sizeof(struct sockaddr_un);
    }
#endif
    return sizeof(sockaddr_u);
}

const char* sockaddr_str(sockaddr_u* addr, char* buf, int len) {
    char ip[SOCKADDR_STRLEN] = {0};
    uint16_t port = 0;
    if (addr->sa.sa_family == AF_INET) {
        inet_ntop(AF_INET, &addr->sin.sin_addr, ip, len);
        port = ntohs(addr->sin.sin_port);
        snprintf(buf, len, "%s:%d", ip, port);
    }
    else if (addr->sa.sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &addr->sin6.sin6_addr, ip, len);
        port = ntohs(addr->sin6.sin6_port);
        snprintf(buf, len, "[%s]:%d", ip, port);
    }
#ifdef ENABLE_UDS
    else if (addr->sa.sa_family == AF_UNIX) {
        snprintf(buf, len, "%s", addr->sun.sun_path);
    }
#endif
    return buf;
}

static int sockaddr_bind(sockaddr_u* localaddr, int type) {
    // socket -> setsockopt -> bind
    int sockfd = socket(localaddr->sa.sa_family, type, 0);
    if (sockfd < 0) {
        perror("socket");
        return socket_errno_negative();
    }


#ifdef SO_REUSEADDR
    {
        // NOTE: SO_REUSEADDR allow to reuse sockaddr of TIME_WAIT status
        int reuseaddr = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(int)) < 0) {
            perror("setsockopt");
            goto error;
        }
    }
#endif

/*
#ifdef SO_REUSEPORT
    {
        // NOTE: SO_REUSEPORT allow multiple sockets to bind same port
        int reuseport = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseport, sizeof(int)) < 0) {
            perror("setsockopt");
            goto error;
        }
    }
#endif
*/

    if (bind(sockfd, &localaddr->sa, sockaddr_len(localaddr)) < 0) {
        perror("bind");
        goto error;
    }

    return sockfd;
error:
    closesocket(sockfd);
    return socket_errno_negative();
}

static int sockaddr_connect(sockaddr_u* peeraddr, int nonblock) {
    // socket -> nonblocking -> connect
    int connfd = socket(peeraddr->sa.sa_family, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        return socket_errno_negative();
    }

    if (nonblock) {
        nonblocking(connfd);
    }

    int ret = connect(connfd, &peeraddr->sa, sockaddr_len(peeraddr));
#ifdef OS_WIN
    if (ret < 0 && socket_errno() != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && socket_errno() != EINPROGRESS) {
#endif
        perror("connect");
        closesocket(connfd);
        return socket_errno_negative();
    }
    return connfd;
}

static int ListenFD(int sockfd) {
    if (sockfd < 0) return sockfd;
    if (listen(sockfd, SOMAXCONN) < 0) {
        perror("listen");
        closesocket(sockfd);
        return socket_errno_negative();
    }
    return sockfd;
}

static int ConnectFDTimeout(int connfd, int ms) {
    int err;
    socklen_t optlen = sizeof(err);
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
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
    return socket_errno_negative();
}

int Bind(int port, const char* host, int type) {
#ifdef OS_WIN
    if (s_wsa_initialized == 0) {
        s_wsa_initialized = 1;
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2,2), &wsadata);
    }
#endif
    sockaddr_u localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    int ret = sockaddr_set_ipport(&localaddr, host, port);
    if (ret != 0) {
        return NABS(ret);
    }
    return sockaddr_bind(&localaddr, type);
}

int Listen(int port, const char* host) {
    int sockfd = Bind(port, host, SOCK_STREAM);
    if (sockfd < 0) return sockfd;
    return ListenFD(sockfd);
}

int Connect(const char* host, int port, int nonblock) {
#ifdef OS_WIN
    if (s_wsa_initialized == 0) {
        s_wsa_initialized = 1;
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2,2), &wsadata);
    }
#endif
    sockaddr_u peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    int ret = sockaddr_set_ipport(&peeraddr, host, port);
    if (ret != 0) {
        return NABS(ret);
    }
    return sockaddr_connect(&peeraddr, nonblock);
}

int ConnectNonblock(const char* host, int port) {
    return Connect(host, port, 1);
}

int ConnectTimeout(const char* host, int port, int ms) {
    int connfd = Connect(host, port, 1);
    if (connfd < 0) return connfd;
    return ConnectFDTimeout(connfd, ms);
}

#ifdef ENABLE_UDS
int BindUnix(const char* path, int type) {
    sockaddr_u localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    sockaddr_set_path(&localaddr, path);
    return sockaddr_bind(&localaddr, type);
}

int ListenUnix(const char* path) {
    int sockfd = BindUnix(path, SOCK_STREAM);
    if (sockfd < 0) return sockfd;
    return ListenFD(sockfd);
}

int ConnectUnix(const char* path, int nonblock) {
    sockaddr_u peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    sockaddr_set_path(&peeraddr, path);
    return sockaddr_connect(&peeraddr, nonblock);
}

int ConnectUnixNonblock(const char* path) {
    return ConnectUnix(path, 1);
}

int ConnectUnixTimeout(const char* path, int ms) {
    int connfd = ConnectUnix(path, 1);
    if (connfd < 0) return connfd;
    return ConnectFDTimeout(connfd, ms);
}
#endif

int Socketpair(int family, int type, int protocol, int sv[2]) {
#ifdef OS_UNIX
    if (family == AF_UNIX) {
        return socketpair(family, type, protocol, sv);
    }
#endif
    if (family != AF_INET || type != SOCK_STREAM) {
        return -1;
    }
    int listenfd, connfd, acceptfd;
    listenfd = connfd = acceptfd = INVALID_SOCKET;
    struct sockaddr_in localaddr;
    socklen_t addrlen = sizeof(localaddr);
    memset(&localaddr, 0, addrlen);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    localaddr.sin_port = 0;
    // listener
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        goto error;
    }
    if (bind(listenfd, (struct sockaddr*)&localaddr, addrlen) < 0) {
        perror("bind");
        goto error;
    }
    if (listen(listenfd, 1) < 0) {
        perror("listen");
        goto error;
    }
    if (getsockname(listenfd, (struct sockaddr*)&localaddr, &addrlen) < 0) {
        perror("getsockname");
        goto error;
    }
    // connector
    connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        goto error;
    }
    if (connect(connfd, (struct sockaddr*)&localaddr, addrlen) < 0) {
        perror("connect");
        goto error;
    }
    // acceptor
    acceptfd = accept(listenfd, (struct sockaddr*)&localaddr, &addrlen);
    if (acceptfd < 0) {
        perror("accept");
        goto error;
    }

    closesocket(listenfd);
    sv[0] = connfd;
    sv[1] = acceptfd;
    return 0;
error:
    if (listenfd != INVALID_SOCKET) {
        closesocket(listenfd);
    }
    if (connfd != INVALID_SOCKET) {
        closesocket(connfd);
    }
    if (acceptfd != INVALID_SOCKET) {
        closesocket(acceptfd);
    }
    return -1;
}
