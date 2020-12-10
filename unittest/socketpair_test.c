#include <stdio.h>

#include "hsocket.h"

int main(int argc, char* argv[]) {
#ifdef OS_WIN
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2,2), &wsadata);
#endif

    int sockfds[2];
    if (Socketpair(AF_INET, SOCK_STREAM, 0, sockfds) != 0) {
        printf("socketpair failed!\n");
        return -1;
    }
    printf("Socketpair %d<=>%d\n", sockfds[0], sockfds[1]);

    char sendbuf[] = "hello,world!";
    char recvbuf[1460];
    int nsend = send(sockfds[0], sendbuf, strlen(sendbuf), 0);
    printf("sockfd:%d send %d bytes: %s\n", sockfds[0], nsend, sendbuf);
    memset(recvbuf, 0, sizeof(recvbuf));
    int nrecv = recv(sockfds[1], recvbuf, sizeof(recvbuf), 0);
    printf("sockfd:%d recv %d bytes: %s\n", sockfds[1], nrecv, recvbuf);

    closesocket(sockfds[0]);
    closesocket(sockfds[1]);
    return 0;
}
