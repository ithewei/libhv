#include <assert.h>
#include <stdio.h>
#include <string.h>

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

int main(void) {
    test_server_context_accepts_matching_pkcs1_identity();
    test_server_context_accepts_matching_pkcs8_identity();
    test_server_context_rejects_missing_identity();
    test_context_rejects_partial_identity();
    test_context_rejects_mismatched_identity();
    test_context_loads_ca_file();
    test_context_loads_ca_directory_and_skips_noise();
    test_context_rejects_empty_ca_source();
    puts("appletls_test: PASS");
    return 0;
}
