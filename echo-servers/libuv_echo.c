#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uv.h"

typedef struct {
    uv_write_t  req;
    uv_buf_t    buf;
} uv_write_req_t;

void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
}

void close_cb(uv_handle_t* handle) {
    free(handle);
}

void write_cb(uv_write_t* req, int status) {
    uv_write_req_t* wr = (uv_write_req_t*)req;
    free(wr->buf.base);
    free(wr);
}

void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread <= 0) {
        uv_close((uv_handle_t*)stream, close_cb);
        return;
    }
    uv_write_req_t* wr = (uv_write_req_t*)malloc(sizeof(uv_write_req_t));
    wr->buf.base = buf->base;
    wr->buf.len = nread;
    uv_write(&wr->req, stream, &wr->buf, 1, write_cb);
}

void do_accept(uv_stream_t* server, int status) {
    uv_tcp_t* tcp_stream = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(server->loop, tcp_stream);
    uv_accept(server, (uv_stream_t*)tcp_stream);
    uv_read_start((uv_stream_t*)tcp_stream, alloc_cb, read_cb);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    uv_loop_t loop;
    uv_loop_init(&loop);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    //addr.sin_family = AF_INET;
    //addr.sin_port = htons(port);
    uv_ip4_addr("0.0.0.0", port, &addr);

    uv_tcp_t tcp_server;
    uv_tcp_init(&loop, &tcp_server);
    int ret = uv_tcp_bind(&tcp_server, (struct sockaddr*)&addr, 0);
    if (ret) {
        return -20;
    }
    ret = uv_listen((uv_stream_t*)&tcp_server, SOMAXCONN, do_accept);
    if (ret) {
        return -30;
    }

    uv_run(&loop, UV_RUN_DEFAULT);
    return 0;
}
