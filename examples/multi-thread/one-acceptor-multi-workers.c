/*
 *
 * @build   make examples
 * @server  bin/one-acceptor-multi-workers 1234
 * @client  bin/nc 127.0.0.1 1234
 *          nc     127.0.0.1 1234
 *          telnet 127.0.0.1 1234
 */

#include "hloop.h"
#include "hsocket.h"
#include "hthread.h"

static const char* host = "0.0.0.0";
static int port = 1234;
static int thread_num = 4;
static hloop_t*  accept_loop = NULL;
static hloop_t** worker_loops = NULL;

static hloop_t* get_next_loop() {
    static int s_cur_index = 0;
    if (s_cur_index == thread_num) {
        s_cur_index = 0;
    }
    return worker_loops[s_cur_index++];
}

static void on_close(hio_t* io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    // echo
    hio_write(io, buf, readbytes);
}

static void new_conn_event(hevent_t* ev) {
    hloop_t* loop = ev->loop;
    hio_t* io = (hio_t*)hevent_userdata(ev);
    hio_attach(loop, io);

    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("tid=%ld connfd=%d [%s] <= [%s]\n",
            (long)hv_gettid(),
            (int)hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
}

static void on_accept(hio_t* io) {
    hio_detach(io);

    hloop_t* worker_loop = get_next_loop();
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.loop = worker_loop;
    ev.cb = new_conn_event;
    ev.userdata = io;
    hloop_post_event(worker_loop, &ev);
}

static HTHREAD_RETTYPE worker_thread(void* userdata) {
    hloop_t* loop = (hloop_t*)userdata;
    hloop_run(loop);
    return 0;
}

static HTHREAD_RETTYPE accept_thread(void* userdata) {
    hloop_t* loop = (hloop_t*)userdata;
    hio_t* listenio = hloop_create_tcp_server(loop, host, port, on_accept);
    if (listenio == NULL) {
        exit(1);
    }
    hloop_run(loop);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cmd port\n");
        return -10;
    }
    port = atoi(argv[1]);

    worker_loops = (hloop_t**)malloc(sizeof(hloop_t*) * thread_num);
    for (int i = 0; i < thread_num; ++i) {
        worker_loops[i] = hloop_new(HLOOP_FLAG_AUTO_FREE);
        hthread_create(worker_thread, worker_loops[i]);
    }

    accept_loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    accept_thread(accept_loop);

    return 0;
}
