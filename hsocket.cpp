#include "hsocket.h"

#ifdef OS_WIN
class WinSocketRAII {
public:
    WinSocketRAII() {
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2,2), &wsadata);
    }
    ~WinSocketRAII() {
        WSACleanup();
    }
};
static WinSocketRAII s_ws;
#endif

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
        return -11;
    }
    struct sockaddr_in addr;
    unsigned int addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&addr, addrlen) < 0) {
        perror("bind");
        return -20;
    }
    if (listen(listenfd, SOMAXCONN) < 0) {
        perror("listen");
        return -30;
    }
    return listenfd;
}

int Connect(const char* host, int port, int nonblock) {
    // gethostbyname -> socket -> nonblocking -> connect
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host);
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
    if (ret < 0 && sockerrno != EINPROGRESS) {
        perror("connect");
        return -30;
    }
    return connfd;
}
