#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event2/event.h"
#include "event2/listener.h"
#include "event2/bufferevent.h"
#include "event2/buffer.h"

//#define RECV_BUFSIZE    8192

void error_cb(struct bufferevent* bev, short event, void* userdata) {
    bufferevent_free(bev);
}

void read_cb(struct bufferevent* bev, void* userdata) {
    //static char recvbuf[RECV_BUFSIZE];
    //int nread = bufferevent_read(bev, &recvbuf, RECV_BUFSIZE);
    //bufferevent_write(bev, recvbuf, nread);
    struct evbuffer* buf = evbuffer_new();
    int ret = bufferevent_read_buffer(bev, buf);
    if (ret == 0) {
        bufferevent_write_buffer(bev, buf);
    }
    evbuffer_free(buf);
}

void on_accept(struct evconnlistener* listener, evutil_socket_t connfd, struct sockaddr* peeraddr, int addrlen, void* userdata) {
    struct event_base* loop = evconnlistener_get_base(listener);
    struct bufferevent* bev = bufferevent_socket_new(loop, connfd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(bev, read_cb, NULL, error_cb, NULL);
    bufferevent_enable(bev, EV_READ|EV_WRITE|EV_PERSIST);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    int port = atoi(argv[1]);

    struct event_base* loop = event_base_new();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    struct evconnlistener* listener =  evconnlistener_new_bind(
            loop, on_accept, NULL,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
            -1, (struct sockaddr*)&addr, sizeof(addr));
    if (listener == NULL) {
        return -20;
    }

    event_base_dispatch(loop);

    evconnlistener_free(listener);
    event_base_free(loop);
    return 0;
}
