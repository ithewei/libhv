/*
 * network client
 *
 * @build:  make examples
 * @server  bin/httpd -s restart -d
 * @usage:  bin/nc 127.0.0.1 8080
 *          > GET / HTTP/1.1
 *          > Connection: close
 *          > [Enter]
 *          > GET / HTTP/1.1
 *          > Connection: keep-alive
 *          > [Enter]
 */

/*
 * @test    udp client
 * @build   ./configure && make examples
 * @client  bin/nc -u 127.0.0.1 1234
 *
 */

/*
 * @test    ssl client
 * @build   ./configure --with-openssl && make clean && make
 * @client  bin/nc -s 127.0.0.1 1234
 *
 */

/*
 * @test    kcp client
 * @build   ./configure --with-kcp && make clean && make
 * @client  bin/nc -k 127.0.0.1 1234
 *
 */

#include "hloop.h"
#include "hbase.h"
#include "hsocket.h"
#include "hssl.h"

#define RECV_BUFSIZE    8192
static char recvbuf[RECV_BUFSIZE];

static char protocol = 't';
static const char* protocolname = "tcp";
// for stdin
static hio_t* stdinio = NULL;
// for socket
static hio_t* sockio = NULL;

static int verbose = 0;

static void send_heartbeat(hio_t* io) {
    static char buf[] = "PING\r\n";
    // printf("send_heartbeat %s", buf);
    hio_write(io, buf, 6);
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("[%s] <=> [%s]\n",
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }
    printf("%.*s", readbytes, (char*)buf);

    // test hio_set_readbuf in hread_cb
#if 0
    static int total_readbytes = 0;
    total_readbytes += readbytes;
    if (total_readbytes >= RECV_BUFSIZE) {
        total_readbytes = 0;
    }
    hio_set_readbuf(io, recvbuf + total_readbytes, RECV_BUFSIZE - total_readbytes);
    printf("%.*s", total_readbytes, recvbuf);
#endif

    fflush(stdout);
}

static void on_stdin(hio_t* io, void* buf, int readbytes) {
    // printf("on_stdin fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    // printf("> %s\n", buf);

    char* str = (char*)buf;

    // test hio_read_start/hio_read_stop/hio_close/hloop_stop
#if 1
    if (strncmp(str, "start", 5) == 0) {
        printf("call hio_read_start\n");
        hio_read_start(sockio);
        return;
    }
    else if (strncmp(str, "stop", 4) == 0) {
        printf("call hio_read_stop\n");
        hio_read_stop(sockio);
        return;
    }
    else if (strncmp(str, "close", 5) == 0) {
        printf("call hio_close\n");
        hio_close(sockio);
        return;
    }
    else if (strncmp(str, "quit", 4) == 0) {
        printf("call hloop_stop\n");
        hloop_stop(hevent_loop(io));
        return;
    }
#endif

    // CR|LF => CRLF for test most protocols
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

    if (strncmp(str, "CLOSE", 5) == 0) {
        printf("call hio_close\n");
        hio_close(sockio);
    }
}

static void on_close(hio_t* io) {
    // printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    hio_del(stdinio, HV_READ);
}

static void on_connect(hio_t* io) {
    // printf("on_connect fd=%d\n", hio_fd(io));
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("connect connfd=%d [%s] => [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }

    hio_read_start(io);
    // uncomment to test heartbeat
    // hio_set_heartbeat(sockio, 3000, send_heartbeat);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("\
Usage: nc [-tusk] host port\n\
Options:\n\
  -t        Use tcp protocol (default)\n\
  -u        Use udp protocol\n\
  -s        Use ssl protocol\n\
  -k        Use kcp protocol\n\
Examples: nc 127.0.0.1 80\n\
          nc -u 127.0.0.1 80\n");
        return -10;
    }

    int index = 1;
    if (argv[1][0] == '-') {
        protocol = argv[1][1];
        switch(protocol) {
        case 't': protocolname = "tcp"; break;
        case 'u': protocolname = "udp"; break;
        case 's': protocolname = "ssl"; break;
        case 'k': protocolname = "kcp"; break;
        default:  fprintf(stderr, "Unsupported protocol '%c'\n", protocol); exit(1);
        }
        ++index;
    }
    const char* host = argv[index++];
    int port = atoi(argv[index++]);
    if (verbose) {
        printf("%s %s %d\n", protocolname, host, port);
    }

    HV_MEMCHECK;

    hloop_t* loop = hloop_new(HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);

    // stdin use default readbuf
    stdinio = hread(loop, 0, NULL, 0, on_stdin);
    if (stdinio == NULL) {
        return -20;
    }

    // socket
    if (protocol == 't' || protocol == 's') {
        // tcp
        sockio = hloop_create_tcp_client(loop, host, port, on_connect, on_close);
        if (sockio == NULL) {
            return -20;
        }
        if (protocol == 's') {
            if (strcmp(hssl_backend(), "nossl") == 0) {
                fprintf(stderr, "Please recompile WITH_SSL!\n");
                exit(1);
            }
            hio_enable_ssl(sockio);
        }
    }
    else if (protocol == 'u' || protocol == 'k') {
        // udp
        sockio = hloop_create_udp_client(loop, host, port);
        if (sockio == NULL) {
            return -20;
        }
        if (protocol == 'k') {
#if WITH_KCP
            static kcp_setting_t s_kcp_setting;
            kcp_setting_init_with_normal_mode(&s_kcp_setting);
            s_kcp_setting.conv = hv_rand(1, 999999);
            hio_set_kcp(sockio, &s_kcp_setting);
#else
            fprintf(stderr, "Please recompile WITH_KCP!\n");
            exit(1);
#endif
        }
        hio_read(sockio);
    }
    // printf("sockfd=%d\n", hio_fd(sockio));
    hio_setcb_close(sockio, on_close);
    hio_setcb_read(sockio, on_recv);
    hio_set_readbuf(sockio, recvbuf, RECV_BUFSIZE);

    hloop_run(loop);
    hloop_free(&loop);

    return 0;
}
