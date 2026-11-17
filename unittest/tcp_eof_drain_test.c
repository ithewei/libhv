#include "hloop.h"
#include "hsocket.h"

static char response[512 * 1024];
static size_t received;
static int server_closes;

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "tcp_eof_drain_test: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void on_read(hio_t* peer, void* data, int len) {
    (void)peer;
    require(received + len <= sizeof(response) && memcmp(response + received, data, len) == 0, "unexpected response bytes");
    received += len;
}

static void on_server_close(hio_t* io) {
    require(hio_error(io) == 0, "server closed with an error");
    ++server_closes;
}

static void on_peer_close(hio_t* peer) {
    require(hio_error(peer) == 0, "peer closed with an error");
    hloop_stop(hevent_loop(peer));
}

static void on_timeout(htimer_t* timer) {
    (void)timer;
    require(0, "response did not drain before timeout");
}

static int check_drain(int close_before_eof) {
    hloop_t* loop = hloop_new(0);
    int listener = Listen(0, "127.0.0.1");
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    require(listener >= 0 && getsockname(listener, (struct sockaddr*)&addr, &addrlen) == 0, "could not listen on loopback");
    int client = socket(AF_INET, SOCK_STREAM, 0);
    require(client >= 0 && so_rcvbuf(client, 4096) == 0, "could not set up client");
    require(connect(client, (struct sockaddr*)&addr, addrlen) == 0, "could not connect client");
    int server = accept(listener, NULL, NULL);
    closesocket(listener);
    require(server >= 0 && so_sndbuf(server, 4096) == 0, "could not set up server");

    // Wait for the client's FIN before either endpoint starts processing events.
#ifdef OS_WIN
    require(shutdown(client, SD_SEND) == 0, "client half-close failed");
#else
    require(shutdown(client, SHUT_WR) == 0, "client half-close failed");
#endif
    char byte;
    require(recv(server, &byte, 1, MSG_PEEK) == 0, "server did not receive EOF");

    hio_t* io = hio_get(loop, server);
    hio_t* peer = hio_get(loop, client);
    received = 0;
    server_closes = 0;
    hio_setcb_close(io, on_server_close);
    hio_setcb_read(peer, on_read);
    hio_setcb_close(peer, on_peer_close);
    require(hio_read(io) == 0 && hio_read(peer) == 0, "could not watch both sockets");
    require(hio_write(io, response, sizeof(response)) >= 0 && hio_write_bufsize(io) > 0, "response was not queued");
    if (close_before_eof) hio_close(io);
    htimer_add(loop, on_timeout, 10000, 1);
    hloop_run(loop);
    hloop_free(&loop);
    if (received != sizeof(response) || server_closes != 1) {
        fprintf(stderr, "%s: received %zu of %zu bytes, server closed %d times\n", close_before_eof ? "local close first" : "EOF first", received,
                sizeof(response), server_closes);
        return 1;
    }
    return 0;
}

int main(void) {
    for (size_t i = 0; i < sizeof(response); ++i) response[i] = (char)(i % 251);
    int errors = check_drain(0);
    errors += check_drain(1);
    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
