#include "nlog.h"

#include "list.h"
#include "hdef.h"
#include "hsocket.h"

typedef struct network_logger_s {
    hloop_t*            loop;
    hio_t*              listenio;
    struct list_head    clients;
} network_logger_t;

typedef struct nlog_client {
    hio_t*              io;
    struct list_node    node;
} nlog_client;

static network_logger_t s_logger = {0};

static void on_close(hio_t* io) {
    printd("on_close fd=%d error=%d\n", io->fd, io->error);
    struct list_node* next = s_logger.clients.next;
    nlog_client* client;
    while (next != &s_logger.clients) {
        client = list_entry(next, nlog_client, node);
        next = next->next;
        if (client->io == io) {
            list_del(next->prev);
            SAFE_FREE(client);
            break;
        }
    }
}

static void on_read(hio_t* io, void* buf, int readbytes) {
    printd("on_read fd=%d readbytes=%d\n", io->fd, readbytes);
    printd("< %s\n", buf);
    // nothing to do
}

static void on_accept(hio_t* io, int connfd) {
    printd("on_accept listenfd=%d connfd=%d\n", io->fd, connfd);
    char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
    char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
    printd("accept listenfd=%d connfd=%d [%s] <= [%s]\n", io->fd, connfd,
            sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
            sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));

    static char s_readbuf[256] = {0};
    hio_t* connio = hread(io->loop, connfd, s_readbuf, sizeof(s_readbuf), on_read);
    connio->close_cb = on_close;

    // free on_close
    nlog_client* client;
    SAFE_ALLOC_SIZEOF(client);
    client->io = connio;
    list_add(&client->node, &s_logger.clients);
}

void network_logger(int loglevel, const char* buf, int len) {
    struct list_node* node;
    nlog_client* client;
    list_for_each (node, &s_logger.clients) {
        client = list_entry(node, nlog_client, node);
        hwrite(s_logger.loop, client->io->fd, buf, len, NULL);
    }
}

hio_t* nlog_listen(hloop_t* loop, int port) {
    list_init(&s_logger.clients);
    s_logger.loop = loop;
    s_logger.listenio = create_tcp_server(loop, port, on_accept);
    return s_logger.listenio;
}
