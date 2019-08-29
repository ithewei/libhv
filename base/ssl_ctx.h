#ifndef HW_SSL_CTX_H
#define HW_SSL_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

extern void* g_ssl_ctx; // for SSL_CTX

int ssl_ctx_init(const char* crt_file, const char* key_file, const char* ca_file);
int ssl_ctx_destory();

#ifdef __cplusplus
}
#endif

#endif // HW_SSL_CTX_H
