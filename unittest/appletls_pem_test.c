#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "appletls_pem.h"

#define FIXTURE(name) "unittest/fixtures/appletls/" name

static void assert_file_equal(const appletls_der_t* lhs, const appletls_der_t* rhs) {
    assert(lhs->len == rhs->len);
    assert(memcmp(lhs->data, rhs->data, lhs->len) == 0);
}

static char* write_temp(const void* data, size_t len) {
    char* path = strdup("/private/tmp/libhv-appletls-pem-XXXXXX");
    assert(path != NULL);
    int fd = mkstemp(path);
    assert(fd >= 0);
    const unsigned char* ptr = (const unsigned char*)data;
    while (len != 0) {
        ssize_t nwrite = write(fd, ptr, len);
        assert(nwrite > 0);
        ptr += nwrite;
        len -= (size_t)nwrite;
    }
    assert(close(fd) == 0);
    return path;
}

static void remove_temp(char* path) {
    assert(unlink(path) == 0);
    free(path);
}

static void test_load_pkcs1_rsa(void) {
    appletls_der_t key = {0};
    assert(appletls_pem_load_rsa_private_key(FIXTURE("server-pkcs1.key"), &key) == APPLETLS_PEM_OK);
    assert(key.data != NULL && key.len > 256);
    assert(key.data[0] == 0x30);
    appletls_der_free(&key);
    assert(key.data == NULL && key.len == 0);
}

static void test_unwrap_pkcs8_rsa(void) {
    appletls_der_t pkcs1 = {0};
    appletls_der_t pkcs8 = {0};
    assert(appletls_pem_load_rsa_private_key(FIXTURE("server-pkcs1.key"), &pkcs1) == APPLETLS_PEM_OK);
    assert(appletls_pem_load_rsa_private_key(FIXTURE("server-pkcs8.key"), &pkcs8) == APPLETLS_PEM_OK);
    assert_file_equal(&pkcs1, &pkcs8);
    appletls_der_free(&pkcs1);
    appletls_der_free(&pkcs8);
}

static void test_load_certificate_chain_in_order(void) {
    appletls_der_list_t chain = {0};
    appletls_der_list_t leaf = {0};
    appletls_der_list_t intermediate = {0};
    assert(appletls_pem_load_certificates(FIXTURE("server-chain.pem"), 0, 64, &chain) == APPLETLS_PEM_OK);
    assert(appletls_pem_load_certificates(FIXTURE("server.crt"), 0, 1, &leaf) == APPLETLS_PEM_OK);
    assert(appletls_pem_load_certificates(FIXTURE("intermediate.crt"), 0, 1, &intermediate) == APPLETLS_PEM_OK);
    assert(chain.count == 2 && leaf.count == 1 && intermediate.count == 1);
    assert_file_equal(&chain.items[0], &leaf.items[0]);
    assert_file_equal(&chain.items[1], &intermediate.items[0]);
    appletls_der_list_free(&chain);
    appletls_der_list_free(&leaf);
    appletls_der_list_free(&intermediate);
}

static void test_reject_ec_and_encrypted_keys(void) {
    appletls_der_t key = {0};
    assert(appletls_pem_load_rsa_private_key(FIXTURE("ec-sec1.key"), &key) == APPLETLS_PEM_ERROR_UNSUPPORTED);
    assert(appletls_pem_load_rsa_private_key(FIXTURE("ec-pkcs8.key"), &key) == APPLETLS_PEM_ERROR_UNSUPPORTED);
    assert(appletls_pem_load_rsa_private_key(FIXTURE("encrypted-key.pem"), &key) == APPLETLS_PEM_ERROR_UNSUPPORTED);
}

static void test_reject_invalid_base64(void) {
    appletls_der_list_t certs = {0};
    assert(appletls_pem_load_certificates(FIXTURE("malformed-base64.pem"), 0, 1, &certs) == APPLETLS_PEM_ERROR_FORMAT);
}

static void test_reject_invalid_der_lengths(void) {
    static const unsigned char indefinite[] = {0x30, 0x80, 0x00, 0x00};
    static const unsigned char nonminimal[] = {0x30, 0x81, 0x01, 0x00};
    static const unsigned char truncated[] = {0x30, 0x04, 0x02, 0x01};
    static const unsigned char trailing[] = {0x30, 0x00, 0x00};
    const unsigned char* cases[] = {indefinite, nonminimal, truncated, trailing};
    const size_t sizes[] = {sizeof(indefinite), sizeof(nonminimal), sizeof(truncated), sizeof(trailing)};
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char* path = write_temp(cases[i], sizes[i]);
        appletls_der_list_t certs = {0};
        assert(appletls_pem_load_certificates(path, 1, 1, &certs) == APPLETLS_PEM_ERROR_FORMAT);
        remove_temp(path);
    }
}

static void test_reject_non_rsa_pkcs8_oid(void) {
    appletls_der_t key = {0};
    assert(appletls_pem_load_rsa_private_key(FIXTURE("ec-pkcs8.key"), &key) == APPLETLS_PEM_ERROR_UNSUPPORTED);
    assert(appletls_pem_load_rsa_private_key(FIXTURE("malformed-pkcs8.pem"), &key) == APPLETLS_PEM_ERROR_FORMAT);
}

static void test_reject_multiple_private_keys(void) {
    FILE* source = fopen(FIXTURE("server-pkcs1.key"), "rb");
    assert(source != NULL);
    assert(fseek(source, 0, SEEK_END) == 0);
    long length = ftell(source);
    assert(length > 0 && fseek(source, 0, SEEK_SET) == 0);
    char* key = (char*)malloc((size_t)length);
    char* twice = (char*)malloc((size_t)length * 2);
    assert(key != NULL && twice != NULL);
    assert(fread(key, 1, (size_t)length, source) == (size_t)length);
    fclose(source);
    memcpy(twice, key, (size_t)length);
    memcpy(twice + length, key, (size_t)length);
    char* path = write_temp(twice, (size_t)length * 2);
    appletls_der_t parsed = {0};
    assert(appletls_pem_load_rsa_private_key(path, &parsed) == APPLETLS_PEM_ERROR_FORMAT);
    remove_temp(path);
    free(twice);
    free(key);
}

static void test_enforce_file_block_and_count_limits(void) {
    appletls_der_list_t certs = {0};
    char* path;
    int fd;
    assert(appletls_pem_load_certificates(FIXTURE("server-chain.pem"), 0, 1, &certs) == APPLETLS_PEM_ERROR_LIMIT);

    path = strdup("/private/tmp/libhv-appletls-large-XXXXXX");
    assert(path != NULL);
    fd = mkstemp(path);
    assert(fd >= 0);
    assert(ftruncate(fd, (off_t)APPLETLS_PEM_MAX_FILE_SIZE + 1) == 0);
    assert(close(fd) == 0);
    assert(appletls_pem_load_certificates(path, 1, 1, &certs) == APPLETLS_PEM_ERROR_LIMIT);
    remove_temp(path);
}

int main(void) {
    test_load_pkcs1_rsa();
    test_unwrap_pkcs8_rsa();
    test_load_certificate_chain_in_order();
    test_reject_ec_and_encrypted_keys();
    test_reject_invalid_base64();
    test_reject_invalid_der_lengths();
    test_reject_non_rsa_pkcs8_oid();
    test_reject_multiple_private_keys();
    test_enforce_file_block_and_count_limits();
    puts("appletls_pem_test: PASS");
    return 0;
}
