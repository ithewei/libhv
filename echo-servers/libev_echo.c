#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "ev.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

void do_recv(struct ev_loop *loop, struct ev_io *io, int revents) {
    int nread, nsend;
    nread = recv(io->fd, recvbuf, RECV_BUFSIZE, 0);
    if (nread <= 0) {
        goto error;
    }
    nsend = send(io->fd, recvbuf, nread, 0);
    if (nsend != nread) {
        goto error;
    }
    return;

error:
    ev_io_stop(loop, io);
    close(io->fd);
    free(io);
}

void do_accept(struct ev_loop *loop, struct ev_io *listenio, int revents) {
    struct sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    int connfd = accept(listenio->fd, (struct sockaddr*)&peeraddr, &addrlen);
    if (connfd <= 0) {
        return;
    }

    struct ev_io* io = (struct ev_io*)malloc(sizeof(struct ev_io));
    ev_io_init(io, do_recv, connfd, EV_READ);
    ev_io_start(loop, io);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        return -20;
    }
    if (bind(listenfd, (struct sockaddr*)&addr, addrlen) < 0) {
        return -30;
    }
    if (listen(listenfd, SOMAXCONN) < 0) {
        return -40;
    }

    struct ev_loop* loop = ev_loop_new(0);

    struct ev_io listenio;
    ev_io_init(&listenio, do_accept, listenfd, EV_READ);
    ev_io_start(loop, &listenio);

    ev_run(loop, 0);
    ev_loop_destroy(loop);
    return 0;
}
