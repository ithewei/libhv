#include "hloop.h"
#include "hbase.h"
#include "hsocket.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

// 1:tcp 2:udp
int protocol = 1;
// for stdin
hio_t*      stdinio = NULL;
// for socket
hio_t*      sockio = NULL;

const char* src_path = NULL;

int verbose = 1;

void cleanup(int signo) {
    if (protocol > 2) {
        printf("cleaning up: %s\n", src_path);
        unlink(src_path);
    }
    exit(0);
}

void on_recv(hio_t* io, void* buf, int readbytes) {
    //printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("[%s] <=> [%s]\n",
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }
    printf("%.*s", readbytes, (char*)buf);
    fflush(stdout);
}

void on_stdin(hio_t* io, void* buf, int readbytes) {
    //printf("on_stdin fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    //printf("> %s\n", buf);

    // CR|LF => CRLF for test most protocols
    char* str = (char*)buf;
    char eol = str[readbytes-1];
    if (eol == '\n' || eol == '\r') {
        if (readbytes > 1 && str[readbytes-2] == '\r' && eol == '\n') {
            // have been CRLF
        }
        else {
            ++readbytes;
            str[readbytes - 2] = '\r';
            str[readbytes - 1] = '\n';
        }
    }
    hio_write(sockio, buf, readbytes);
}

void on_close(hio_t* io) {
    //printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    hio_del(stdinio, HV_READ);
}

void on_connect(hio_t* io) {
    //printf("on_connect fd=%d\n", hio_fd(io));
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("connect connfd=%d [%s] => [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }

    hio_read(io);
}

int main(int argc, char** argv) {
    const char* protocol_name;
    const char* host;
    int port;
    const char* dest_path;
    if (argc == 3) {
        protocol = 1;
        protocol_name = "tcp";
        host = argv[1];
        port = atoi(argv[2]);
    }
    else if (argc == 4) {
        if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "-u") == 0) {
            if (argv[1][1] == 't') {
                protocol = 1;
                protocol_name = "tcp";
            } else {
                protocol = 2;
                protocol_name = "udp";
            }
            host = argv[2];
            port = atoi(argv[3]);
        }
        else if (strcmp(argv[1], "-U") == 0 || strcmp(argv[1], "-D") == 0) {
            if (argv[1][1] == 'U') {
                protocol = 3;
                protocol_name = "unix(stream)";
            } else {
                protocol = 4;
                protocol_name = "unix(datagram)";
            }
            src_path = argv[2];
            dest_path = argv[3];
        }
        else {
            goto bad_args;
        }
    }
    else {
bad_args:
        printf("Usage: cmd [-ut] host port\n"
               "       cmd [-UD] source dest\n"
               "Options:\n"
               "  -t        Use tcp protocol (default)\n"
               "  -u        Use udp protocol\n"
               "  -U        Use Unix domain socket (SOCK_STREAM)\n"
               "  -D        Use Unix domain socket (SOCK_DGRAM)\n"
               "Examples: nc 127.0.0.1 80\n"
               "          nc -u 127.0.0.1 80\n"
               "          nc -D ~/src.sock ~/dest.sock\n");
        return -10;
    }
    if (verbose) {
        if (protocol > 2) {
            printf("%s: %s -> %s\n", protocol_name, src_path, dest_path);
        }
        else {
            printf("%s: %s:%d\n", protocol_name, host, port);
        }
    }

    MEMCHECK;

    hloop_t* loop = hloop_new(0);

    // stdin
    stdinio = hread(loop, 0, recvbuf, RECV_BUFSIZE, on_stdin);
    if (stdinio == NULL) {
        return -20;
    }

    // socket
    if (protocol == 1) {
        // tcp
        sockio = create_tcp_client(loop, host, port, on_connect);
    }
    else if (protocol == 2) {
        // udp
        sockio = create_udp_client(loop, host, port);
        hio_read(sockio);
    }
#ifdef HAVE_UDS
    else if (protocol == 3) {
        // Unix domain socket (SOCK_STREAM)
        sockio = create_unix_stream_client(loop, dest_path, src_path, on_connect);
    }
    else if (protocol == 4) {
        // Unix domain socket (SOCK_DGRAM)
        sockio = create_unix_dgram_client(loop, dest_path, src_path);
    }
#else
    else {
        printf("Unix domain socket is not supported!\n");
    }
#endif
    if (sockio == NULL) {
        return -20;
    }
    //printf("sockfd=%d\n", hio_fd(sockio));
    hio_setcb_close(sockio, on_close);
    hio_setcb_read(sockio, on_recv);
    hio_set_readbuf(sockio, recvbuf, RECV_BUFSIZE);

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    hloop_run(loop);
    hloop_free(&loop);
    if (protocol > 2) {
        unlink(src_path);
    }

    return 0;
}
