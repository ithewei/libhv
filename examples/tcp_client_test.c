/*
 * tcp client demo
 *
 * @build   make examples
 * @server  bin/tcp_echo_server 1234
 * @client  bin/tcp_client_test 127.0.0.1 1234
 *
 */

#include "hloop.h"
#include "hssl.h"
#include "hmutex.h"

#include "hbase.h"
#include "herr.h"

#define TEST_SSL        0
#define TEST_UNPACK     0
#define TEST_RECONNECT  1

// @see mqtt/mqtt_client.h
typedef struct tcp_client_s {
    // connect: host:port
    char host[256];
    int  port;
    int  connect_timeout; // ms
    // reconnect
    reconn_setting_t* reconn_setting;
    // flags
    unsigned char   ssl: 1; // Read Only
    unsigned char   alloced_ssl_ctx: 1; // intern
    unsigned char   connected : 1;
    // privdata
    hloop_t*    loop;
    hio_t*      io;
    htimer_t*   reconn_timer;
    // SSL/TLS
    hssl_ctx_t  ssl_ctx;
    // thread-safe
    hmutex_t    mutex_;
    // ...
} tcp_client_t;

static tcp_client_t* tcp_client_new(hloop_t* loop DEFAULT(NULL));
static void          tcp_client_run (tcp_client_t* cli);
static void          tcp_client_stop(tcp_client_t* cli);
static void          tcp_client_free(tcp_client_t* cli);

// SSL/TLS
static int tcp_client_set_ssl_ctx(tcp_client_t* cli, hssl_ctx_t ssl_ctx);
static int tcp_client_new_ssl_ctx(tcp_client_t* cli, hssl_ctx_opt_t* opt);

// reconnect
static int tcp_client_set_reconnect(tcp_client_t* cli, reconn_setting_t* reconn);
static int tcp_client_reconnect(tcp_client_t* cli);

static void tcp_client_set_connnect_timeout(tcp_client_t* cli, int timeout_ms);
static int  tcp_client_connect(tcp_client_t* cli, const char* host, int port, int ssl);
static int  tcp_client_disconnect(tcp_client_t* cli);
static bool tcp_client_is_connected(tcp_client_t* cli);

static int  tcp_client_send(tcp_client_t* cli, const void* buf, int len);

static void reconnect_timer_cb(htimer_t* timer) {
    tcp_client_t* cli = (tcp_client_t*)hevent_userdata(timer);
    if (cli == NULL) return;
    cli->reconn_timer = NULL;
    tcp_client_reconnect(cli);
}

static void on_close(hio_t* io) {
    printf("onclose: connfd=%d error=%d\n", hio_fd(io), hio_error(io));
    tcp_client_t* cli = (tcp_client_t*)hevent_userdata(io);
    cli->connected = 0;
    // reconnect
    if (cli->reconn_setting && reconn_setting_can_retry(cli->reconn_setting)) {
        uint32_t delay = reconn_setting_calc_delay(cli->reconn_setting);
        printf("reconnect cnt=%d, delay=%d ...\n", cli->reconn_setting->cur_retry_cnt, cli->reconn_setting->cur_delay);
        cli->reconn_timer = htimer_add(cli->loop, reconnect_timer_cb, delay, 1);
        hevent_set_userdata(cli->reconn_timer, cli);
    }
}

static void on_message(hio_t* io, void* buf, int len) {
    printf("onmessage: %.*s\n", len, (char*)buf);
    tcp_client_t* cli = (tcp_client_t*)hevent_userdata(io);
    // ...
}

static void on_connect(hio_t* io) {
    printf("onconnect: connfd=%d\n", hio_fd(io));
    tcp_client_t* cli = (tcp_client_t*)hevent_userdata(io);
    cli->connected = 1;

#if TEST_UNPACK
    static unpack_setting_t s_unpack_setting;
    s_unpack_setting.mode = UNPACK_BY_DELIMITER;
    s_unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
    s_unpack_setting.delimiter_bytes = 2;
    s_unpack_setting.delimiter[0] = '\r';
    s_unpack_setting.delimiter[1] = '\n';
    hio_set_unpack(io, &s_unpack_setting);
#endif

    hio_write(io, "hello\r\n", 7);

    hio_setcb_read(io, on_message);
    hio_read(io);
}

// hloop_new -> malloc(tcp_client_t)
tcp_client_t* tcp_client_new(hloop_t* loop) {
    if (loop == NULL) {
        loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
        if (loop == NULL) return NULL;
    }
    tcp_client_t* cli = NULL;
    HV_ALLOC_SIZEOF(cli);
    if (cli == NULL) return NULL;
    cli->loop = loop;
    hmutex_init(&cli->mutex_);
    return cli;
}

// hloop_free -> free(tcp_client_t)
void tcp_client_free(tcp_client_t* cli) {
    if (!cli) return;
    hmutex_destroy(&cli->mutex_);
    if (cli->reconn_timer) {
        htimer_del(cli->reconn_timer);
        cli->reconn_timer = NULL;
    }
    if (cli->ssl_ctx && cli->alloced_ssl_ctx) {
        hssl_ctx_free(cli->ssl_ctx);
        cli->ssl_ctx = NULL;
    }
    HV_FREE(cli->reconn_setting);
    HV_FREE(cli);
}

void tcp_client_run (tcp_client_t* cli) {
    if (!cli || !cli->loop) return;
    hloop_run(cli->loop);
}

void tcp_client_stop(tcp_client_t* cli) {
    if (!cli || !cli->loop) return;
    hloop_stop(cli->loop);
}

int tcp_client_set_ssl_ctx(tcp_client_t* cli, hssl_ctx_t ssl_ctx) {
    cli->ssl_ctx = ssl_ctx;
    return 0;
}

// hssl_ctx_new(opt) -> tcp_client_set_ssl_ctx
int tcp_client_new_ssl_ctx(tcp_client_t* cli, hssl_ctx_opt_t* opt) {
    opt->endpoint = HSSL_CLIENT;
    hssl_ctx_t ssl_ctx = hssl_ctx_new(opt);
    if (ssl_ctx == NULL) return ERR_NEW_SSL_CTX;
    cli->alloced_ssl_ctx = true;
    return tcp_client_set_ssl_ctx(cli, ssl_ctx);
}

int tcp_client_set_reconnect(tcp_client_t* cli, reconn_setting_t* reconn) {
    if (reconn == NULL) {
        HV_FREE(cli->reconn_setting);
        return 0;
    }
    if (cli->reconn_setting == NULL) {
        HV_ALLOC_SIZEOF(cli->reconn_setting);
    }
    *cli->reconn_setting = *reconn;
    return 0;
}

int tcp_client_reconnect(tcp_client_t* cli) {
    tcp_client_connect(cli, cli->host, cli->port, cli->ssl);
    return 0;
}

int tcp_client_connect(tcp_client_t* cli, const char* host, int port, int ssl) {
    if (!cli) return -1;
    hv_strncpy(cli->host, host, sizeof(cli->host));
    cli->port = port;
    cli->ssl = ssl;
    hio_t* io = hio_create_socket(cli->loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) return -1;
    if (ssl) {
        if (cli->ssl_ctx) {
            hio_set_ssl_ctx(io, cli->ssl_ctx);
        }
        hio_enable_ssl(io);
    }
    if (cli->connect_timeout > 0) {
        hio_set_connect_timeout(io, cli->connect_timeout);
    }
    cli->io = io;
    hevent_set_userdata(io, cli);
    hio_setcb_connect(io, on_connect);
    hio_setcb_close(io, on_close);
    return hio_connect(io);
}

int tcp_client_disconnect(tcp_client_t* cli) {
    if (!cli || !cli->io) return -1;
    // cancel reconnect first
    tcp_client_set_reconnect(cli, NULL);
    return hio_close(cli->io);
}

bool tcp_client_is_connected(tcp_client_t* cli) {
    return cli && cli->connected;
}

int tcp_client_send(tcp_client_t* cli, const void* buf, int len) {
    if (!cli || !cli->io || !buf || len == 0) return -1;
    if (!cli->connected) return -2;
    // thread-safe
    hmutex_lock(&cli->mutex_);
    int nwrite = hio_write(cli->io, buf, len);
    hmutex_unlock(&cli->mutex_);
    return nwrite;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s host port\n", argv[0]);
        return -10;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);

    tcp_client_t* cli = tcp_client_new(NULL);
    if (!cli) return -20;

#if TEST_RECONNECT
    reconn_setting_t reconn;
    reconn_setting_init(&reconn);
    reconn.min_delay = 1000;
    reconn.max_delay = 10000;
    reconn.delay_policy = 2;
    tcp_client_set_reconnect(cli, &reconn);
#endif

    int ssl = 0;
#if TEST_SSL
    ssl = 1;
#endif
    tcp_client_connect(cli, host, port, ssl);

    tcp_client_run(cli);
    tcp_client_free(cli);
    return 0;
}
