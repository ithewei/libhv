#include "hloop.h"

#include "htime.h"
#include "hsocket.h"

#define RECV_BUFSIZE    8192

void on_timer(htimer_t* timer, void* userdata) {
    static int cnt = 0;
    printf("on_timer timer_id=%d time=%luus cnt=%d\n", timer->timer_id, timer->loop->cur_time, ++cnt);
}

void on_idle(hidle_t* idle, void* userdata) {
    static int cnt = 0;
    printf("on_idle idle_id=%d cnt=%d\n", idle->idle_id, ++cnt);
}

void on_read(hevent_t* event, void* userdata) {
    printf("on_read fd=%d\n", event->fd);
    char recvbuf[RECV_BUFSIZE] = {0};
    int nrecv;
recv:
    memset(recvbuf, 0, sizeof(recvbuf));
    nrecv = recv(event->fd, recvbuf, sizeof(recvbuf), 0);
    printf("recv retval=%d\n", nrecv);
    if (nrecv < 0) {
        if (sockerrno != NIO_EAGAIN) {
            //goto recv_done;
            return;
        }
        else {
            perror("recv");
            goto recv_error;
        }
    }
    if (nrecv == 0) {
        goto disconnect;
    }
    printf("> %s\n", recvbuf);
    if (nrecv == sizeof(recvbuf)) {
        goto recv;
    }

recv_error:
disconnect:
    printf("closesocket fd=%d\n", event->fd);
    closesocket(event->fd);
    hevent_del(event);
}

void on_connect(hevent_t* event, void* userdata) {
    printf("on_connect connfd=%d\n", event->fd);
    struct sockaddr_in localaddr,peeraddr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int ret = getpeername(event->fd, (struct sockaddr*)&peeraddr, &addrlen);
    if (ret < 0) {
        printf("connect failed: %s: %d\n", strerror(sockerrno), sockerrno);
        closesocket(event->fd);
        return;
    }
    addrlen = sizeof(struct sockaddr_in);
    getsockname(event->fd, (struct sockaddr*)&localaddr, &addrlen);
    printf("connect connfd=%d [%s:%d] => [%s:%d]\n", event->fd,
            inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port),
            inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port));

    hevent_read(event->loop, event->fd, on_read, NULL);

    static const char* http_request = "GET / HTTP/1.1\r\n\r\n";
    int nsend = send(event->fd, http_request, strlen(http_request), 0);
    printf("send retval=%d\n", nsend);
    printf("< %s\n", http_request);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: cmd host port\n");
        return -10;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);

    int connfd = Connect(host, port, 1);
    printf("connfd=%d\n", connfd);
    if (connfd < 0) {
        return connfd;
    }

    hloop_t loop;
    hloop_init(&loop);
    //hidle_add(&loop, on_idle, NULL);
    htimer_add(&loop, on_timer, NULL, 1000, INFINITE);
    hevent_connect(&loop, connfd, on_connect, NULL);
    hloop_run(&loop);

    return 0;
}
