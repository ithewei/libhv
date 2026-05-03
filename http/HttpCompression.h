#ifndef HV_HTTP_COMPRESSION_H_
#define HV_HTTP_COMPRESSION_H_

#include "hexport.h"

#include <stddef.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

typedef enum http_content_encoding {
    HTTP_CONTENT_ENCODING_UNKNOWN = -1,
    HTTP_CONTENT_ENCODING_IDENTITY = 0,
    HTTP_CONTENT_ENCODING_GZIP,
    HTTP_CONTENT_ENCODING_ZSTD,
} http_content_encoding;

enum {
    HTTP_CONTENT_ENCODING_IDENTITY_MASK = 1u << HTTP_CONTENT_ENCODING_IDENTITY,
    HTTP_CONTENT_ENCODING_GZIP_MASK = 1u << HTTP_CONTENT_ENCODING_GZIP,
    HTTP_CONTENT_ENCODING_ZSTD_MASK = 1u << HTTP_CONTENT_ENCODING_ZSTD,
};

#define HTTP_CONTENT_ENCODING_SUPPORTED_MASK \
    (HTTP_CONTENT_ENCODING_IDENTITY_MASK | HTTP_CONTENT_ENCODING_GZIP_MASK | HTTP_CONTENT_ENCODING_ZSTD_MASK)

BEGIN_EXTERN_C

HV_EXPORT const char* http_content_encoding_str(http_content_encoding encoding);
HV_EXPORT http_content_encoding http_content_encoding_enum(const char* encoding);
HV_EXPORT unsigned http_content_encoding_supported_mask();
HV_EXPORT int http_content_encoding_is_available(http_content_encoding encoding);

END_EXTERN_C

struct HV_EXPORT HttpCompressionOptions {
    bool enabled;
    bool decompress_request;
    bool compress_request;
    bool decompress_response;
    bool compress_response;
    bool advertise_accept_encoding;
    unsigned enabled_encodings;
    http_content_encoding preferred_encoding;
    size_t min_length;
    size_t max_decoded_size;

#ifdef __cplusplus
    HttpCompressionOptions()
        : enabled(false)
        , decompress_request(false)
        , compress_request(false)
        , decompress_response(false)
        , compress_response(false)
        , advertise_accept_encoding(false)
        , enabled_encodings(HTTP_CONTENT_ENCODING_IDENTITY_MASK)
        , preferred_encoding(HTTP_CONTENT_ENCODING_UNKNOWN)
        , min_length(256)
        , max_decoded_size(64u << 20) {}
#endif
};

struct HV_EXPORT WebSocketCompressionOptions {
    bool enabled;
    bool client_no_context_takeover;
    bool server_no_context_takeover;
    int  client_max_window_bits;
    int  server_max_window_bits;
    size_t min_length;
    size_t max_decoded_size;

#ifdef __cplusplus
    WebSocketCompressionOptions();
#endif
};

#ifdef __cplusplus
namespace hv {

HV_EXPORT HttpCompressionOptions DefaultServerCompressionOptions();
HV_EXPORT HttpCompressionOptions DefaultClientCompressionOptions();
HV_EXPORT WebSocketCompressionOptions DefaultWebSocketCompressionOptions();

}
#endif

#endif // HV_HTTP_COMPRESSION_H_
