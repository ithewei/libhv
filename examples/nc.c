#include "hloop.h"
#include "hbase.h"
#include "hsocket.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

// 1:tcp 2:udp
int protocol = 1;
// for stdin
hio_t*      stdinio = NULL;
// for udp
struct sockaddr* peeraddr = NULL;
socklen_t peeraddrlen = sizeof(struct sockaddr_in6);
// for tcp
hio_t*      sockio = NULL;

int verbose = 0;

void on_recv(hio_t* io, void* buf, int readbytes) {
    //printf("on_recv fd=%d readbytes=%d\n", io->fd, readbytes);
    if (verbose) {
        char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
        char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
        printf("[%s] <=> [%s]\n",
                sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
                sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
    }
    printf("%s", (char*)buf);
    fflush(stdout);
}

void on_stdin(hio_t* io, void* buf, int readbytes) {
    //printf("on_stdin fd=%d readbytes=%d\n", io->fd, readbytes);
    //printf("> %s\n", buf);

    if (protocol == 1) {
        hsend(sockio->loop, sockio->fd, buf, readbytes, NULL);
    }
    else if (protocol == 2) {
        hsendto(sockio->loop, sockio->fd, buf, readbytes, NULL);
        hrecvfrom(sockio->loop, sockio->fd, recvbuf, RECV_BUFSIZE, on_recv);
    }
}

void on_close(hio_t* io) {
    //printf("on_close fd=%d error=%d\n", io->fd, io->error);
    hio_del(stdinio, READ_EVENT);
}

void on_connect(hio_t* io, int state) {
    //printf("on_connect fd=%d state=%d\n", io->fd, state);
    if (state == 0) {
        printf("connect failed: %d: %s\n", io->error, strerror(io->error));
        return;
    }
    if (verbose) {
        char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
        char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
        printf("connect connfd=%d [%s] => [%s]\n", io->fd,
                sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
                sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
    }

    hrecv(io->loop, io->fd, recvbuf, RECV_BUFSIZE, on_recv);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("\
Usage: cmd [-ut] host port\n\
Options:\n\
  -t        Use tcp protocol (default)\n\
  -u        Use udp protocol\n\
Examples: nc 127.0.0.1 80\n\
          nc -u 127.0.0.1 80\n");
        return -10;
    }

    int index = 1;
    const char* protocolname;
    if (argv[1][0] == '-') {
        ++index;
        if (argv[1][1] == 't') {
            protocol = 1;
            protocolname = "tcp";
        }
        else if (argv[1][1] == 'u') {
            protocol = 2;
            protocolname = "udp";
        }
    }
    const char* host = argv[index++];
    int port = atoi(argv[index++]);
    if (verbose) {
        printf("%s %s %d\n", protocolname, host, port);
    }

    MEMCHECK;

    hloop_t loop;
    hloop_init(&loop);

    // stdin
    stdinio = hread(&loop, 0, recvbuf, RECV_BUFSIZE, on_stdin);
    if (stdinio == NULL) {
        return -20;
    }

    // socket
    if (protocol == 1) {
        // tcp
        sockio = create_tcp_client(&loop, host, port, on_connect);
    }
    else if (protocol == 2) {
        // udp
        sockio = create_udp_client(&loop, host, port);
    }
    if (sockio == NULL) {
        return -20;
    }
    //printf("sockfd=%d\n", sockio->fd);
    sockio->close_cb = on_close;

    hloop_run(&loop);

    return 0;
}
