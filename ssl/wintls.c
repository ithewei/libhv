#include "hssl.h"

#ifdef WITH_WINTLS

const char* hssl_backend() {
    return "nossl";
}

hssl_ctx_t hssl_ctx_new(hssl_ctx_opt_t* opt) {
    fprintf(stderr, "Please recompile WITH_SSL.\n");
    return NULL;
}

void hssl_ctx_free(hssl_ctx_t ssl_ctx) {
}

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd) {
    return (void*)(intptr_t)fd;
}

void hssl_free(hssl_t ssl) {
}

int hssl_accept(hssl_t ssl) {
    return 0;
}

int hssl_connect(hssl_t ssl) {
    return 0;
}

int hssl_read(hssl_t ssl, void* buf, int len) {
    int fd = (intptr_t)ssl;
    return read(fd, buf, len);
}

int hssl_write(hssl_t ssl, const void* buf, int len) {
    int fd = (intptr_t)ssl;
    return write(fd, buf, len);
}

int hssl_close(hssl_t ssl) {
    return 0;
}

int hssl_set_sni_hostname(hssl_t ssl, const char* hostname) {
    return 0;
}

#endif // WITH_WINTLS
