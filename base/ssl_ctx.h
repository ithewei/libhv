#ifndef HV_SSL_CTX_H
#define HV_SSL_CTX_H

#include "hexport.h"

BEGIN_EXTERN_C

HV_EXPORT void* ssl_ctx_instance();
HV_EXPORT int ssl_ctx_init(const char* crt_file, const char* key_file, const char* ca_file);
HV_EXPORT int ssl_ctx_destory();

END_EXTERN_C

#endif // HV_SSL_CTX_H
