#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hsocket.h"
#include "hssl.h"

#define FIXTURE(name) "unittest/fixtures/appletls/" name

static hssl_ctx_opt_t server_options(const char* key_file) {
    hssl_ctx_opt_t options;
    memset(&options, 0, sizeof(options));
    options.endpoint = HSSL_SERVER;
    options.crt_file = FIXTURE("server-chain.pem");
    options.key_file = key_file;
    return options;
}

static void test_server_context_accepts_matching_pkcs1_identity(void) {
    hssl_ctx_opt_t options = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_t ctx = hssl_ctx_new(&options);
    assert(ctx != NULL);
    hssl_ctx_free(ctx);
}

static void test_server_context_accepts_matching_pkcs8_identity(void) {
    hssl_ctx_opt_t options = server_options(FIXTURE("server-pkcs8.key"));
    hssl_ctx_t ctx = hssl_ctx_new(&options);
    assert(ctx != NULL);
    hssl_ctx_free(ctx);
}

static void test_server_context_rejects_missing_identity(void) {
    hssl_ctx_opt_t options;
    memset(&options, 0, sizeof(options));
    options.endpoint = HSSL_SERVER;
    assert(hssl_ctx_new(&options) == NULL);
}

static void test_context_rejects_partial_identity(void) {
    hssl_ctx_opt_t options = server_options(NULL);
    assert(hssl_ctx_new(&options) == NULL);
    options = server_options(FIXTURE("server-pkcs1.key"));
    options.crt_file = NULL;
    assert(hssl_ctx_new(&options) == NULL);
}

static void test_context_rejects_mismatched_identity(void) {
    hssl_ctx_opt_t options = server_options(FIXTURE("wrong-server.key"));
    assert(hssl_ctx_new(&options) == NULL);
}

static void test_context_loads_ca_file(void) {
    hssl_ctx_opt_t options;
    memset(&options, 0, sizeof(options));
    options.endpoint = HSSL_CLIENT;
    options.verify_peer = 1;
    options.ca_file = FIXTURE("multi-ca.pem");
    hssl_ctx_t ctx = hssl_ctx_new(&options);
    assert(ctx != NULL);
    hssl_ctx_free(ctx);
}

static void test_context_loads_ca_directory_and_skips_noise(void) {
    hssl_ctx_opt_t options;
    memset(&options, 0, sizeof(options));
    options.endpoint = HSSL_CLIENT;
    options.verify_peer = 1;
    options.ca_path = FIXTURE("ca-dir");
    hssl_ctx_t ctx = hssl_ctx_new(&options);
    assert(ctx != NULL);
    hssl_ctx_free(ctx);
}

static void test_context_rejects_empty_ca_source(void) {
    hssl_ctx_opt_t options;
    memset(&options, 0, sizeof(options));
    options.endpoint = HSSL_CLIENT;
    options.verify_peer = 1;
    options.ca_file = FIXTURE("malformed-base64.pem");
    assert(hssl_ctx_new(&options) == NULL);
    options.ca_file = NULL;
    options.ca_path = FIXTURE("empty-ca-dir");
    assert(hssl_ctx_new(&options) == NULL);
}

static void test_invalid_sni_and_identityless_server_fail_cleanly(void) {
    hssl_ctx_t ctx = hssl_ctx_new(NULL);
    hssl_t ssl;
    assert(ctx != NULL);
    ssl = hssl_new(ctx, -1);
    assert(ssl != NULL);
    assert(hssl_set_sni_hostname(ssl, NULL) == HSSL_ERROR);
    assert(hssl_set_sni_hostname(ssl, "") == HSSL_ERROR);
    assert(hssl_accept(ssl) == HSSL_ERROR);
    hssl_free(ssl);
    hssl_ctx_free(ctx);
}

typedef struct {
    hssl_ctx_t server_ctx;
    hssl_ctx_t client_ctx;
    hssl_t server;
    hssl_t client;
    int fds[2];
} tls_pair_t;

static void tls_pair_close(tls_pair_t* pair) {
    if (pair->server) hssl_free(pair->server);
    if (pair->client) hssl_free(pair->client);
    if (pair->server_ctx) hssl_ctx_free(pair->server_ctx);
    if (pair->client_ctx) hssl_ctx_free(pair->client_ctx);
    if (pair->fds[0] >= 0) close(pair->fds[0]);
    if (pair->fds[1] >= 0) close(pair->fds[1]);
    memset(pair, 0, sizeof(*pair));
    pair->fds[0] = pair->fds[1] = -1;
}

static int tls_pair_open(tls_pair_t* pair, const hssl_ctx_opt_t* server_options,
                         const hssl_ctx_opt_t* client_options, const char* hostname) {
    int i;
    int server_status = HSSL_WANT_READ;
    int client_status = HSSL_WANT_READ;
    memset(pair, 0, sizeof(*pair));
    pair->fds[0] = pair->fds[1] = -1;
    pair->server_ctx = hssl_ctx_new((hssl_ctx_opt_t*)server_options);
    pair->client_ctx = hssl_ctx_new((hssl_ctx_opt_t*)client_options);
    if (pair->server_ctx == NULL || pair->client_ctx == NULL) goto error;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair->fds) != 0) goto error;
    if (nonblocking(pair->fds[0]) != 0 || nonblocking(pair->fds[1]) != 0) goto error;
    pair->server = hssl_new(pair->server_ctx, pair->fds[0]);
    pair->client = hssl_new(pair->client_ctx, pair->fds[1]);
    if (pair->server == NULL || pair->client == NULL) goto error;
    if (hostname && hssl_set_sni_hostname(pair->client, hostname) != HSSL_OK) goto error;

    for (i = 0; i < 10000; ++i) {
        if (server_status != HSSL_OK) server_status = hssl_accept(pair->server);
        if (client_status != HSSL_OK) client_status = hssl_connect(pair->client);
        if (server_status == HSSL_ERROR || client_status == HSSL_ERROR) goto error;
        if (server_status == HSSL_OK && client_status == HSSL_OK) return 0;
    }

error:
    tls_pair_close(pair);
    return -1;
}

static void assert_bidirectional_data(tls_pair_t* pair) {
    static const char request[] = "client-to-server";
    static const char response[] = "server-to-client";
    char buffer[64];
    int ret;
    int i;

    assert(hssl_write(pair->client, request, (int)sizeof(request)) == (int)sizeof(request));
    ret = HSSL_WOULD_BLOCK;
    for (i = 0; i < 1000 && ret == HSSL_WOULD_BLOCK; ++i) {
        ret = hssl_read(pair->server, buffer, sizeof(buffer));
    }
    assert(ret == (int)sizeof(request));
    assert(memcmp(buffer, request, sizeof(request)) == 0);

    assert(hssl_write(pair->server, response, (int)sizeof(response)) == (int)sizeof(response));
    ret = HSSL_WOULD_BLOCK;
    for (i = 0; i < 1000 && ret == HSSL_WOULD_BLOCK; ++i) {
        ret = hssl_read(pair->client, buffer, sizeof(buffer));
    }
    assert(ret == (int)sizeof(response));
    assert(memcmp(buffer, response, sizeof(response)) == 0);
}

static hssl_ctx_opt_t verified_client_options(const char* ca_file) {
    hssl_ctx_opt_t options;
    memset(&options, 0, sizeof(options));
    options.endpoint = HSSL_CLIENT;
    options.verify_peer = 1;
    options.ca_file = ca_file;
    return options;
}

static void test_server_handshake_and_bidirectional_data(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client;
    tls_pair_t pair;
    memset(&client, 0, sizeof(client));
    client.endpoint = HSSL_CLIENT;
    assert(tls_pair_open(&pair, &server, &client, NULL) == 0);
    assert_bidirectional_data(&pair);
    tls_pair_close(&pair);
}

static void test_custom_ca_and_matching_hostname_succeed(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client = verified_client_options(FIXTURE("root.crt"));
    tls_pair_t pair;
    assert(tls_pair_open(&pair, &server, &client, "localhost") == 0);
    tls_pair_close(&pair);
}

static void test_custom_ca_directory_and_matching_hostname_succeed(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client = verified_client_options(NULL);
    tls_pair_t pair;
    client.ca_path = FIXTURE("ca-dir");
    assert(tls_pair_open(&pair, &server, &client, "localhost") == 0);
    tls_pair_close(&pair);
}

static void test_empty_read_is_nonblocking(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client;
    tls_pair_t pair;
    char byte;
    memset(&client, 0, sizeof(client));
    client.endpoint = HSSL_CLIENT;
    assert(tls_pair_open(&pair, &server, &client, NULL) == 0);
    errno = 0;
    assert(hssl_read(pair.client, &byte, 1) == HSSL_WOULD_BLOCK);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    tls_pair_close(&pair);
}

static void test_wrong_hostname_fails(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client = verified_client_options(FIXTURE("root.crt"));
    tls_pair_t pair;
    assert(tls_pair_open(&pair, &server, &client, "wrong.example") != 0);
}

static void test_wrong_custom_ca_fails(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client = verified_client_options(FIXTURE("wrong-root.crt"));
    tls_pair_t pair;
    assert(tls_pair_open(&pair, &server, &client, "localhost") != 0);
}

static void test_server_mtls_rejects_missing_client_certificate(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client = verified_client_options(FIXTURE("root.crt"));
    tls_pair_t pair;
    server.verify_peer = 1;
    server.ca_file = FIXTURE("root.crt");
    assert(tls_pair_open(&pair, &server, &client, "localhost") != 0);
}

static void test_server_mtls_accepts_trusted_client_certificate(void) {
    hssl_ctx_opt_t server = server_options(FIXTURE("server-pkcs1.key"));
    hssl_ctx_opt_t client = verified_client_options(FIXTURE("root.crt"));
    tls_pair_t pair;
    server.verify_peer = 1;
    server.ca_file = FIXTURE("root.crt");
    client.crt_file = FIXTURE("client-chain.pem");
    client.key_file = FIXTURE("client.key");
    assert(tls_pair_open(&pair, &server, &client, "localhost") == 0);
    assert_bidirectional_data(&pair);
    tls_pair_close(&pair);
}

int main(void) {
    test_server_context_accepts_matching_pkcs1_identity();
    test_server_context_accepts_matching_pkcs8_identity();
    test_server_context_rejects_missing_identity();
    test_context_rejects_partial_identity();
    test_context_rejects_mismatched_identity();
    test_context_loads_ca_file();
    test_context_loads_ca_directory_and_skips_noise();
    test_context_rejects_empty_ca_source();
    test_invalid_sni_and_identityless_server_fail_cleanly();
    test_server_handshake_and_bidirectional_data();
    test_custom_ca_and_matching_hostname_succeed();
    test_custom_ca_directory_and_matching_hostname_succeed();
    test_empty_read_is_nonblocking();
    test_wrong_hostname_fails();
    test_wrong_custom_ca_fails();
    test_server_mtls_rejects_missing_client_certificate();
    test_server_mtls_accepts_trusted_client_certificate();
    puts("appletls_test: PASS");
    return 0;
}
