/*
 *
 * @build   make examples
 * @server  bin/multi-acceptor-processes 1234
 * @client  bin/nc 127.0.0.1 1234
 *          nc     127.0.0.1 1234
 *          telnet 127.0.0.1 1234
 */

#include "hloop.h"
#include "hsocket.h"
#include "hthread.h"
#include "hproc.h"

static char protocol = 't';
static const char* protocolname = "tcp";
static const char* host = "0.0.0.0";
static int port = 1234;
static int process_num = 4;

static void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    // echo
    hio_write(io, buf, readbytes);
}

static void on_accept(hio_t* io) {
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("pid=%ld connfd=%d [%s] <= [%s]\n",
            (long)hv_getpid(),
            (int)hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
}

static void loop_proc(void* userdata) {
    int sockfd = (int)(intptr_t)(userdata);
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    hio_t* io = hio_get(loop, sockfd);
    if (protocol == 't') {
        hio_setcb_accept(io, on_accept);
        hio_accept(io);
    }
    else if (protocol == 'u') {
        hio_setcb_read(io, on_recv);
        hio_read(io);
    }
    hloop_run(loop);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int index = 1;
    if (argv[1][0] == '-') {
        protocol = argv[1][1];
        switch(protocol) {
        case 't': protocolname = "tcp"; break;
        case 'u': protocolname = "udp"; break;
        default:  fprintf(stderr, "Unsupported protocol '%c'\n", protocol); exit(1);
        }
        ++index;
    }
    port = atoi(argv[index++]);

    int sockfd = -1;
    if (protocol == 't') {
        sockfd = Listen(port, host);
    }
    else if (protocol == 'u') {
        sockfd = Bind(port, host, SOCK_DGRAM);
    }
    if (sockfd < 0) {
        exit(1);
    }

    proc_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.proc = loop_proc;
    ctx.proc_userdata = (void*)(intptr_t)sockfd;
    for (int i = 0; i < process_num; ++i) {
        hproc_spawn(&ctx);
    }

    while(1) hv_sleep(1);

    return 0;
}
