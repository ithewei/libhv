/*
 * tcp chat server
 *
 * @build   make examples
 * @server  bin/tcp_chat_server 1234
 * @clients bin/nc 127.0.0.1 1234
 *          nc     127.0.0.1 1234
 *          telnet 127.0.0.1 1234
 */

#include "hloop.h"
#include "hsocket.h"
#include "hbase.h"
#include "list.h"

unpack_setting_t unpack_setting;

// hloop_create_tcp_server
// on_accept => join
// on_recv => broadcast
// on_close => leave

typedef struct chatroom_s {
    hloop_t*            loop;
    hio_t*              listenio;
    int                 roomid;
    struct list_head    conns;
} chatroom_t;

typedef struct connection_s {
    hio_t*              connio;
    char                addr[SOCKADDR_STRLEN];
    struct list_node    node;
} connection_t;

static chatroom_t s_chatroom;

static void join(chatroom_t* room, connection_t* conn);
static void leave(chatroom_t* room, connection_t* conn);
static void broadcast(chatroom_t* room, const char* msg, int msglen);

void join(chatroom_t* room, connection_t* conn) {
    list_add(&conn->node, &room->conns);

    char msg[256] = {0};
    int msglen = 0;

    struct list_node* node;
    connection_t* cur;
    msglen = snprintf(msg, sizeof(msg), "room[%06d] clients:\r\n", room->roomid);
    hio_write(conn->connio, msg, msglen);
    list_for_each (node, &room->conns) {
        cur = list_entry(node, connection_t, node);
        msglen = snprintf(msg, sizeof(msg), "[%s]\r\n", cur->addr);
        hio_write(conn->connio, msg, msglen);
    }
    hio_write(conn->connio, "\r\n", 2);

    msglen = snprintf(msg, sizeof(msg), "client[%s] join room[%06d]\r\n", conn->addr, room->roomid);
    broadcast(room, msg, msglen);
}

void leave(chatroom_t* room, connection_t* conn) {
    list_del(&conn->node);

    char msg[256] = {0};
    int msglen = snprintf(msg, sizeof(msg), "client[%s] leave room[%d]\r\n", conn->addr, room->roomid);
    broadcast(room, msg, msglen);
}

void broadcast(chatroom_t* room, const char* msg, int msglen) {
    printf("> %.*s", msglen, msg);
    struct list_node* node;
    connection_t* conn;
    list_for_each (node, &room->conns) {
        conn = list_entry(node, connection_t, node);
        hio_write(conn->connio, msg, msglen);
    }
}

static void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));

    connection_t* conn = (connection_t*)hevent_userdata(io);
    if (conn) {
        hevent_set_userdata(io, NULL);

        leave(&s_chatroom, conn);
        HV_FREE(conn);
    }
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("[%s] <=> [%s]\n",
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    printf("< %.*s", readbytes, (char*)buf);

    // broadcast
    connection_t* conn = (connection_t*)hevent_userdata(io);
    assert(conn != NULL);
    char msg[256] = {0};
    int msglen = snprintf(msg, sizeof(msg), "client[%s] say: %.*s", conn->addr, readbytes, (char*)buf);
    broadcast(&s_chatroom, msg, msglen);
}

static void on_accept(hio_t* io) {
    printf("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_set_unpack(io, &unpack_setting);
    hio_read_start(io);

    // free on_close
    connection_t* conn = NULL;
    HV_ALLOC_SIZEOF(conn);
    conn->connio = io;
    strcpy(conn->addr, peeraddrstr);
    hevent_set_userdata(io, conn);
    join(&s_chatroom, conn);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    memset(&unpack_setting, 0, sizeof(unpack_setting_t));
    unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
    unpack_setting.mode = UNPACK_BY_DELIMITER;
    unpack_setting.delimiter[0] = '\n';
    unpack_setting.delimiter_bytes = 1;

    hloop_t* loop = hloop_new(0);
    hio_t* listenio = hloop_create_tcp_server(loop, "0.0.0.0", port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", hio_fd(listenio));

    s_chatroom.loop = loop;
    s_chatroom.listenio = listenio;
    s_chatroom.roomid = hv_rand(100000, 999999);
    list_init(&s_chatroom.conns);

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
