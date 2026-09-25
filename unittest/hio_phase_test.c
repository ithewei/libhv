#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hevent.h"
#include "hsocket.h"
#include "proxy.h"
#include "tls.h"

#ifdef EVENT_IOCP
int main() {
    printf("hio_phase_test skipped: NIO phase dispatch is not used by IOCP\n");
    return 0;
}
#else

static int s_accept_count = 0;
static int s_connect_count = 0;
static int s_read_count = 0;
static hloop_t* s_loop = NULL;
static hio_t* s_listener = NULL;
static hio_t* s_client = NULL;
static const char s_preconnect_message[] = "queued before connect";

static void test_hio_init_phase() {
    hio_t io;
    memset(&io, 0, sizeof(io));
    io.phase = HIO_PHASE_CLOSED;
    hio_init(&io);
    assert(io.phase == HIO_PHASE_NONE);
    hrecursive_mutex_destroy(&io.write_mutex);
}

static void test_proxy_established_transition() {
    hloop_t* loop = hloop_new(0);
    assert(loop != NULL);
    hio_t* io = hio_create_socket(loop, "127.0.0.1", 1, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    assert(io != NULL);

    proxy_setting_t setting;
    memset(&setting, 0, sizeof(setting));
    setting.protocol = PROXY_PROTOCOL_SOCKS5;
    assert(hio_set_proxy(io, &setting) == 0);
    io->phase = HIO_PHASE_PROXY_HANDSHAKING;

    proxy_handshake_established(io);
    assert(io->phase == HIO_PHASE_PROXY_ESTABLISHED);
    assert(!io->closed);

    hio_close(io);
    hloop_free(&loop);
}

static void test_tls_phase_contract() {
    void (*handshake_step)(hio_t*) = tls_handshake_step;
    assert(handshake_step != NULL);
    assert(HIO_PHASE_TLS_ESTABLISHED != HIO_PHASE_ESTABLISHED);
}

#ifdef WITH_OPENSSL
static hloop_t* s_tls_loop = NULL;
static hio_t* s_tls_listener = NULL;
static hio_t* s_tls_client = NULL;
static int s_tls_accept_count = 0;
static int s_tls_connect_count = 0;

static void finish_tls_if_ready() {
    if (s_tls_accept_count != 1 || s_tls_connect_count != 1) return;
    hloop_stop(s_tls_loop);
}

static void on_tls_timeout(htimer_t* timer) {
    (void)timer;
    assert(s_tls_accept_count == 1);
    assert(s_tls_connect_count == 1);
    hloop_stop(s_tls_loop);
}

static void on_tls_accept(hio_t* io) {
    assert(io->phase == HIO_PHASE_ESTABLISHED);
    ++s_tls_accept_count;
    hio_close(io);
    finish_tls_if_ready();
}

static void on_tls_connect(hio_t* io) {
    assert(io->phase == HIO_PHASE_ESTABLISHED);
    ++s_tls_connect_count;
    finish_tls_if_ready();
}

static void test_tls_handshake_lifecycle() {
    s_tls_loop = hloop_new(0);
    assert(s_tls_loop != NULL);
    s_tls_listener = hloop_create_ssl_server(s_tls_loop, "127.0.0.1", 0, on_tls_accept);
    assert(s_tls_listener != NULL);

    hssl_ctx_opt_t server_opt;
    memset(&server_opt, 0, sizeof(server_opt));
    server_opt.crt_file = "cert/server.crt";
    server_opt.key_file = "cert/server.key";
    server_opt.endpoint = HSSL_SERVER;
    assert(hio_new_ssl_ctx(s_tls_listener, &server_opt) == 0);

    int port = sockaddr_port((sockaddr_u*)hio_localaddr(s_tls_listener));
    assert(port > 0);
    s_tls_client = hio_create_socket(s_tls_loop, "127.0.0.1", port, HIO_TYPE_SSL, HIO_CLIENT_SIDE);
    assert(s_tls_client != NULL);
    hio_setcb_connect(s_tls_client, on_tls_connect);
    assert(hio_connect(s_tls_client) == 0);

    htimer_add(s_tls_loop, on_tls_timeout, 3000, 1);
    assert(hloop_run(s_tls_loop) == 0);
    assert(s_tls_accept_count == 1);
    assert(s_tls_connect_count == 1);
    hio_close(s_tls_client);
    hio_close(s_tls_listener);
    hloop_free(&s_tls_loop);
}
#endif

static void finish_if_ready() {
    if (s_accept_count != 1 || s_connect_count != 1 || s_read_count != 1) return;
    hloop_stop(s_loop);
}

static void on_timeout(htimer_t* timer) {
    (void)timer;
    assert(s_accept_count == 1);
    assert(s_connect_count == 1);
    assert(s_read_count == 1);
    hloop_stop(s_loop);
}

static void on_read(hio_t* io, void* buf, int readbytes) {
    assert(readbytes == (int)sizeof(s_preconnect_message) - 1);
    assert(memcmp(buf, s_preconnect_message, readbytes) == 0);
    ++s_read_count;
    hio_close(io);
    finish_if_ready();
}

static void on_accept(hio_t* io) {
    assert(io->phase == HIO_PHASE_ESTABLISHED);
    ++s_accept_count;
    hio_setcb_read(io, on_read);
    hio_read(io);
    finish_if_ready();
}

static void on_connect(hio_t* io) {
    assert(io->phase == HIO_PHASE_ESTABLISHED);
    ++s_connect_count;
    finish_if_ready();
}

int main() {
    test_hio_init_phase();
    test_proxy_established_transition();
    test_tls_phase_contract();
#ifdef WITH_OPENSSL
    test_tls_handshake_lifecycle();
#endif

    s_loop = hloop_new(0);
    assert(s_loop != NULL);

    s_listener = hio_create_socket(s_loop, "127.0.0.1", 0, HIO_TYPE_TCP, HIO_SERVER_SIDE);
    assert(s_listener != NULL);
    assert(s_listener->phase == HIO_PHASE_READY);
    hio_setcb_accept(s_listener, on_accept);
    assert(hio_accept(s_listener) == 0);
    assert(s_listener->phase == HIO_PHASE_ACCEPTING);
    hio_cb dispatcher = (hio_cb)s_listener->cb;
    assert(dispatcher != NULL);
    assert(hio_del(s_listener, HV_READ) == 0);
    assert(hio_add(s_listener, NULL, HV_READ) == 0);
    assert((hio_cb)s_listener->cb == dispatcher);

    sockaddr_u* addr = (sockaddr_u*)hio_localaddr(s_listener);
    int port = sockaddr_port(addr);
    assert(port > 0);

    s_client = hio_create_socket(s_loop, "127.0.0.1", port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    assert(s_client != NULL);
    assert(s_client->phase == HIO_PHASE_READY);
    hio_setcb_connect(s_client, on_connect);
    assert(hio_connect(s_client) == 0);
    assert(s_client->phase == HIO_PHASE_CONNECTING);
    assert((hio_cb)s_client->cb != NULL);
    assert(hio_write(s_client, s_preconnect_message, sizeof(s_preconnect_message) - 1) == 0);
    assert(hio_write_bufsize(s_client) == sizeof(s_preconnect_message) - 1);

    htimer_add(s_loop, on_timeout, 1000, 1);
    assert(hloop_run(s_loop) == 0);
    assert(s_accept_count == 1);
    assert(s_connect_count == 1);
    assert(s_read_count == 1);
    assert(hio_write_is_complete(s_client));
    hio_close(s_client);
    hio_close(s_listener);
    assert(s_client->phase == HIO_PHASE_CLOSED);
    assert(s_listener->phase == HIO_PHASE_CLOSED);
    hloop_free(&s_loop);
    printf("hio_phase_test passed\n");
    return 0;
}

#endif
