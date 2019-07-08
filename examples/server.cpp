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
    int nrecv, nsend;
recv:
    memset(recvbuf, 0, sizeof(recvbuf));
    nrecv = recv(event->fd, recvbuf, sizeof(recvbuf), 0);
    printf("recv retval=%d\n", nrecv);
    if (nrecv < 0) {
        if (sockerrno != NIO_EAGAIN) {
            goto recv_done;
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

recv_done:
send:
    static const char* http_response = "HTTP/1.1 200 OK\r\n\r\n";
    nsend = send(event->fd, http_response, strlen(http_response), 0);
    printf("send retval=%d\n", nsend);
    printf("< %s\n", http_response);
    return;

recv_error:
disconnect:
    printf("closesocket fd=%d\n", event->fd);
    closesocket(event->fd);
    hevent_del(event);
}

void on_accept(hevent_t* event, void* userdata) {
    printf("on_accept listenfd=%d\n", event->fd);
    struct sockaddr_in localaddr, peeraddr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    getsockname(event->fd, (struct sockaddr*)&localaddr, &addrlen);
accept:
    addrlen = sizeof(struct sockaddr_in);
    int connfd = accept(event->fd, (struct sockaddr*)&peeraddr, &addrlen);
    if (connfd < 0) {
        if (sockerrno != NIO_EAGAIN) {
            perror("accept");
            goto accept_error;
        }
        //goto accept_done;
        return;
    }
    printf("accept connfd=%d [%s:%d] => [%s:%d]\n", connfd,
            inet_ntoa(peeraddr.sin_addr), ntohs(peeraddr.sin_port),
            inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port));

    nonblocking(connfd);
    hevent_read(event->loop, connfd, on_read, NULL);

    goto accept;

accept_error:
    closesocket(event->fd);
    hevent_del(event);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    int listenfd = Listen(port);
    printf("listenfd=%d\n", listenfd);
    if (listenfd < 0) {
        return listenfd;
    }

    hloop_t loop;
    hloop_init(&loop);
    //hidle_add(&loop, on_idle, NULL);
    //htimer_add(&loop, on_timer, NULL, 1000, INFINITE);
    hevent_accept(&loop, listenfd, on_accept, NULL);
    hloop_run(&loop);
}
