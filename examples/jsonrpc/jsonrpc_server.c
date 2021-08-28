/*
 * json rpc server
 *
 * @build   make jsonrpc
 * @server  bin/jsonrpc_server 1234
 * @client  bin/jsonrpc_client 127.0.0.1 1234 add 1 2
 *
 */

#include "hloop.h"
#include "hbase.h"
#include "hsocket.h"

#include "jsonrpc.h"
#include "cJSON.h"
#include "router.h"
#include "handler.h"

// hloop_create_tcp_server -> on_accept -> hio_read -> on_recv -> hio_write

static int verbose = 0;
static unpack_setting_t jsonrpc_unpack_setting;

jsonrpc_router router[] = {
    {"add", calc_add},
    {"sub", calc_sub},
    {"mul", calc_mul},
    {"div", calc_div},
};
#define JSONRPC_ROUTER_NUM  (sizeof(router)/sizeof(router[0]))

static void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

static void on_recv(hio_t* io, void* readbuf, int readbytes) {
    // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("[%s] <=> [%s]\n",
                SOCKADDR_STR(hio_localaddr(io), localaddrstr),
                SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }

    // unpack -> cJSON_Parse -> router -> cJSON_Print -> pack -> hio_write
    // unpack
    jsonrpc_message msg;
    memset(&msg, 0, sizeof(msg));
    int packlen = jsonrpc_unpack(&msg, readbuf, readbytes);
    if (packlen < 0) {
        printf("jsonrpc_unpack failed!\n");
        return;
    }
    assert(packlen == readbytes);

    // cJSON_Parse
    printf("> %.*s\n", msg.head.length, msg.body);
    cJSON* jreq = cJSON_ParseWithLength(msg.body, msg.head.length);
    cJSON* jres = cJSON_CreateObject();
    cJSON* jid = cJSON_GetObjectItem(jreq, "id");
    cJSON* jmethod = cJSON_GetObjectItem(jreq, "method");
    if (cJSON_IsNumber(jid)) {
        long id = cJSON_GetNumberValue(jid);
        cJSON_AddItemToObject(jres, "id", cJSON_CreateNumber(id));
    }
    if (cJSON_IsString(jmethod)) {
        // router
        char* method = cJSON_GetStringValue(jmethod);
        bool found = false;
        for (int i = 0; i < JSONRPC_ROUTER_NUM; ++i) {
            if (strcmp(method, router[i].method) == 0) {
                found = true;
                router[i].handler(jreq, jres);
                break;
            }
        }
        if (!found) {
            not_found(jreq, jres);
        }
    } else {
        bad_request(jreq, jres);
    }

    // cJSON_Print
    memset(&msg, 0, sizeof(msg));
    msg.body = cJSON_PrintUnformatted(jres);
    msg.head.length = strlen(msg.body);
    printf("< %.*s\n", msg.head.length, msg.body);

    // pack
    packlen = jsonrpc_package_length(&msg.head);
    unsigned char* writebuf = NULL;
    HV_ALLOC(writebuf, packlen);
    packlen = jsonrpc_pack(&msg, writebuf, packlen);
    if (packlen > 0) {
        hio_write(io, writebuf, packlen);
    }

    cJSON_Delete(jreq);
    cJSON_Delete(jres);
    cJSON_free((void*)msg.body);
    HV_FREE(writebuf);
}

static void on_accept(hio_t* io) {
    printf("on_accept connfd=%d\n", hio_fd(io));
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
                SOCKADDR_STR(hio_localaddr(io), localaddrstr),
                SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_set_unpack(io, &jsonrpc_unpack_setting);
    hio_read(io);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);

    // init jsonrpc_unpack_setting
    memset(&jsonrpc_unpack_setting, 0, sizeof(unpack_setting_t));
    jsonrpc_unpack_setting.mode = UNPACK_BY_LENGTH_FIELD;
    jsonrpc_unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
    jsonrpc_unpack_setting.body_offset = JSONRPC_HEAD_LENGTH;
    jsonrpc_unpack_setting.length_field_offset = 1;
    jsonrpc_unpack_setting.length_field_bytes = 4;
    jsonrpc_unpack_setting.length_field_coding = ENCODE_BY_BIG_ENDIAN;

    hloop_t* loop = hloop_new(0);
    hio_t* listenio = hloop_create_tcp_server(loop, "0.0.0.0", port, on_accept);
    if (listenio == NULL) {
        return -20;
    }
    printf("listenfd=%d\n", hio_fd(listenio));
    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
