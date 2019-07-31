#include "hsocket.h"

int Listen(int port) {
    // socket -> setsockopt -> bind -> listen
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return -10;
    }
    // note: SO_REUSEADDR means that you can reuse sockaddr of TIME_WAIT status
    int reuseaddr = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(int)) < 0) {
        perror("setsockopt");
        closesocket(listenfd);
        return -11;
    }
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&addr, addrlen) < 0) {
        perror("bind");
        closesocket(listenfd);
        return -20;
    }
    if (listen(listenfd, SOMAXCONN) < 0) {
        perror("listen");
        closesocket(listenfd);
        return -30;
    }
    return listenfd;
}

int Connect(const char* host, int port, int nonblock) {
    // gethostbyname -> socket -> nonblocking -> connect
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* phe = gethostbyname(host);
        if (phe == NULL)    return -10;
        memcpy(&addr.sin_addr, phe->h_addr, phe->h_length);
    }
    addr.sin_port = htons(port);
    int connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        return -20;
    }
    if (nonblock) {
        nonblocking(connfd);
    }
    int ret = connect(connfd, (struct sockaddr*)&addr, addrlen);
#ifdef OS_WIN
    if (ret < 0 && sockerrno != WSAEWOULDBLOCK) {
#else
    if (ret < 0 && sockerrno != EINPROGRESS) {
#endif
        perror("connect");
        closesocket(connfd);
        return -30;
    }
    return connfd;
}
