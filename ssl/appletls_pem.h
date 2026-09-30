#ifndef HV_APPLETLS_PEM_H_
#define HV_APPLETLS_PEM_H_

#include <stddef.h>

#define APPLETLS_PEM_MAX_FILE_SIZE (16u * 1024u * 1024u)
#define APPLETLS_PEM_MAX_BLOCK_SIZE (4u * 1024u * 1024u)
#define APPLETLS_PEM_MAX_CERTIFICATES 1024u

typedef struct {
    unsigned char* data;
    size_t len;
} appletls_der_t;

typedef struct {
    appletls_der_t* items;
    size_t count;
} appletls_der_list_t;

typedef enum {
    APPLETLS_PEM_OK = 0,
    APPLETLS_PEM_ERROR_IO = -1,
    APPLETLS_PEM_ERROR_FORMAT = -2,
    APPLETLS_PEM_ERROR_UNSUPPORTED = -3,
    APPLETLS_PEM_ERROR_LIMIT = -4,
    APPLETLS_PEM_ERROR_NOMEM = -5
} appletls_pem_error_t;

int appletls_pem_load_certificates(const char* path,
                                    int allow_der,
                                    size_t max_certificates,
                                    appletls_der_list_t* certificates);
int appletls_pem_load_rsa_private_key(const char* path,
                                      appletls_der_t* pkcs1_key);
void appletls_der_free(appletls_der_t* der);
void appletls_der_list_free(appletls_der_list_t* list);
const char* appletls_pem_error_string(int error);

#endif
