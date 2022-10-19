#ifndef HV_HTTP_DEF_H_
#define HV_HTTP_DEF_H_

#include "hexport.h"

#define DEFAULT_HTTP_PORT       80
#define DEFAULT_HTTPS_PORT      443

enum http_version { HTTP_V1 = 1, HTTP_V2 = 2 };
enum http_session_type { HTTP_CLIENT, HTTP_SERVER };
enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };
enum http_parser_state {
    HP_START_REQ_OR_RES,
    HP_MESSAGE_BEGIN,
    HP_URL,
    HP_STATUS,
    HP_HEADER_FIELD,
    HP_HEADER_VALUE,
    HP_HEADERS_COMPLETE,
    HP_CHUNK_HEADER,
    HP_BODY,
    HP_CHUNK_COMPLETE,
    HP_MESSAGE_COMPLETE,
    HP_ERROR
};

// http_status
// XX(num, name, string)
#define HTTP_STATUS_MAP(XX)                                                 \
  XX(100, CONTINUE,                        Continue)                        \
  XX(101, SWITCHING_PROTOCOLS,             Switching Protocols)             \
  XX(102, PROCESSING,                      Processing)                      \
  XX(200, OK,                              OK)                              \
  XX(201, CREATED,                         Created)                         \
  XX(202, ACCEPTED,                        Accepted)                        \
  XX(203, NON_AUTHORITATIVE_INFORMATION,   Non-Authoritative Information)   \
  XX(204, NO_CONTENT,                      No Content)                      \
  XX(205, RESET_CONTENT,                   Reset Content)                   \
  XX(206, PARTIAL_CONTENT,                 Partial Content)                 \
  XX(207, MULTI_STATUS,                    Multi-Status)                    \
  XX(208, ALREADY_REPORTED,                Already Reported)                \
  XX(226, IM_USED,                         IM Used)                         \
  XX(300, MULTIPLE_CHOICES,                Multiple Choices)                \
  XX(301, MOVED_PERMANENTLY,               Moved Permanently)               \
  XX(302, FOUND,                           Found)                           \
  XX(303, SEE_OTHER,                       See Other)                       \
  XX(304, NOT_MODIFIED,                    Not Modified)                    \
  XX(305, USE_PROXY,                       Use Proxy)                       \
  XX(307, TEMPORARY_REDIRECT,              Temporary Redirect)              \
  XX(308, PERMANENT_REDIRECT,              Permanent Redirect)              \
  XX(400, BAD_REQUEST,                     Bad Request)                     \
  XX(401, UNAUTHORIZED,                    Unauthorized)                    \
  XX(402, PAYMENT_REQUIRED,                Payment Required)                \
  XX(403, FORBIDDEN,                       Forbidden)                       \
  XX(404, NOT_FOUND,                       Not Found)                       \
  XX(405, METHOD_NOT_ALLOWED,              Method Not Allowed)              \
  XX(406, NOT_ACCEPTABLE,                  Not Acceptable)                  \
  XX(407, PROXY_AUTHENTICATION_REQUIRED,   Proxy Authentication Required)   \
  XX(408, REQUEST_TIMEOUT,                 Request Timeout)                 \
  XX(409, CONFLICT,                        Conflict)                        \
  XX(410, GONE,                            Gone)                            \
  XX(411, LENGTH_REQUIRED,                 Length Required)                 \
  XX(412, PRECONDITION_FAILED,             Precondition Failed)             \
  XX(413, PAYLOAD_TOO_LARGE,               Payload Too Large)               \
  XX(414, URI_TOO_LONG,                    URI Too Long)                    \
  XX(415, UNSUPPORTED_MEDIA_TYPE,          Unsupported Media Type)          \
  XX(416, RANGE_NOT_SATISFIABLE,           Range Not Satisfiable)           \
  XX(417, EXPECTATION_FAILED,              Expectation Failed)              \
  XX(421, MISDIRECTED_REQUEST,             Misdirected Request)             \
  XX(422, UNPROCESSABLE_ENTITY,            Unprocessable Entity)            \
  XX(423, LOCKED,                          Locked)                          \
  XX(424, FAILED_DEPENDENCY,               Failed Dependency)               \
  XX(426, UPGRADE_REQUIRED,                Upgrade Required)                \
  XX(428, PRECONDITION_REQUIRED,           Precondition Required)           \
  XX(429, TOO_MANY_REQUESTS,               Too Many Requests)               \
  XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large) \
  XX(451, UNAVAILABLE_FOR_LEGAL_REASONS,   Unavailable For Legal Reasons)   \
  XX(500, INTERNAL_SERVER_ERROR,           Internal Server Error)           \
  XX(501, NOT_IMPLEMENTED,                 Not Implemented)                 \
  XX(502, BAD_GATEWAY,                     Bad Gateway)                     \
  XX(503, SERVICE_UNAVAILABLE,             Service Unavailable)             \
  XX(504, GATEWAY_TIMEOUT,                 Gateway Timeout)                 \
  XX(505, HTTP_VERSION_NOT_SUPPORTED,      HTTP Version Not Supported)      \
  XX(506, VARIANT_ALSO_NEGOTIATES,         Variant Also Negotiates)         \
  XX(507, INSUFFICIENT_STORAGE,            Insufficient Storage)            \
  XX(508, LOOP_DETECTED,                   Loop Detected)                   \
  XX(510, NOT_EXTENDED,                    Not Extended)                    \
  XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required) \

// HTTP_STATUS_##name
enum http_status {
#define XX(num, name, string) HTTP_STATUS_##name = num,
    HTTP_STATUS_MAP(XX)
#undef XX
    HTTP_CUSTOM_STATUS
};

#define HTTP_STATUS_IS_REDIRECT(status)             \
    (status) == HTTP_STATUS_MOVED_PERMANENTLY   ||  \
    (status) == HTTP_STATUS_FOUND               ||  \
    (status) == HTTP_STATUS_SEE_OTHER           ||  \
    (status) == HTTP_STATUS_TEMPORARY_REDIRECT  ||  \
    (status) == HTTP_STATUS_PERMANENT_REDIRECT

// http_mehtod
// XX(num, name, string)
#define HTTP_METHOD_MAP(XX)         \
  XX(0,  DELETE,      DELETE)       \
  XX(1,  GET,         GET)          \
  XX(2,  HEAD,        HEAD)         \
  XX(3,  POST,        POST)         \
  XX(4,  PUT,         PUT)          \
  /* pathological */                \
  XX(5,  CONNECT,     CONNECT)      \
  XX(6,  OPTIONS,     OPTIONS)      \
  XX(7,  TRACE,       TRACE)        \
  /* WebDAV */                      \
  XX(8,  COPY,        COPY)         \
  XX(9,  LOCK,        LOCK)         \
  XX(10, MKCOL,       MKCOL)        \
  XX(11, MOVE,        MOVE)         \
  XX(12, PROPFIND,    PROPFIND)     \
  XX(13, PROPPATCH,   PROPPATCH)    \
  XX(14, SEARCH,      SEARCH)       \
  XX(15, UNLOCK,      UNLOCK)       \
  XX(16, BIND,        BIND)         \
  XX(17, REBIND,      REBIND)       \
  XX(18, UNBIND,      UNBIND)       \
  XX(19, ACL,         ACL)          \
  /* subversion */                  \
  XX(20, REPORT,      REPORT)       \
  XX(21, MKACTIVITY,  MKACTIVITY)   \
  XX(22, CHECKOUT,    CHECKOUT)     \
  XX(23, MERGE,       MERGE)        \
  /* upnp */                        \
  XX(24, MSEARCH,     M-SEARCH)     \
  XX(25, NOTIFY,      NOTIFY)       \
  XX(26, SUBSCRIBE,   SUBSCRIBE)    \
  XX(27, UNSUBSCRIBE, UNSUBSCRIBE)  \
  /* RFC-5789 */                    \
  XX(28, PATCH,       PATCH)        \
  XX(29, PURGE,       PURGE)        \
  /* CalDAV */                      \
  XX(30, MKCALENDAR,  MKCALENDAR)   \
  /* RFC-2068, section 19.6.1.2 */  \
  XX(31, LINK,        LINK)         \
  XX(32, UNLINK,      UNLINK)       \
  /* icecast */                     \
  XX(33, SOURCE,      SOURCE)       \

// HTTP_##name
enum http_method {
#define XX(num, name, string) HTTP_##name = num,
    HTTP_METHOD_MAP(XX)
#undef XX
    HTTP_CUSTOM_METHOD
};

// MIME: https://www.iana.org/assignments/media-types/media-types.xhtml
// XX(name, mime, suffix)
#define MIME_TYPE_TEXT_MAP(XX) \
    XX(TEXT_PLAIN,              text/plain,               txt)          \
    XX(TEXT_HTML,               text/html,                html)         \
    XX(TEXT_CSS,                text/css,                 css)          \
    XX(TEXT_CSV,                text/csv,                 csv)          \
    XX(TEXT_MARKDOWN,           text/markdown,            md)           \
    XX(TEXT_EVENT_STREAM,       text/event-stream,        sse)          \

#define MIME_TYPE_APPLICATION_MAP(XX) \
    XX(APPLICATION_JAVASCRIPT,  application/javascript,             js)     \
    XX(APPLICATION_JSON,        application/json,                   json)   \
    XX(APPLICATION_XML,         application/xml,                    xml)    \
    XX(APPLICATION_URLENCODED,  application/x-www-form-urlencoded,  kv)     \
    XX(APPLICATION_OCTET_STREAM,application/octet-stream,           bin)    \
    XX(APPLICATION_ZIP,         application/zip,                    zip)    \
    XX(APPLICATION_GZIP,        application/gzip,                   gzip)   \
    XX(APPLICATION_7Z,          application/x-7z-compressed,        7z)     \
    XX(APPLICATION_RAR,         application/x-rar-compressed,       rar)    \
    XX(APPLICATION_PDF,         application/pdf,                    pdf)    \
    XX(APPLICATION_RTF,         application/rtf,                    rtf)    \
    XX(APPLICATION_GRPC,        application/grpc,                   grpc)   \
    XX(APPLICATION_WASM,        application/wasm,                   wasm)   \
    XX(APPLICATION_JAR,         application/java-archive,           jar)    \
    XX(APPLICATION_XHTML,       application/xhtml+xml,              xhtml)  \
    XX(APPLICATION_ATOM,        application/atom+xml,               atom)   \
    XX(APPLICATION_RSS,         application/rss+xml,                rss)    \
    XX(APPLICATION_WORD,        application/msword,                 doc)    \
    XX(APPLICATION_EXCEL,       application/vnd.ms-excel,           xls)    \
    XX(APPLICATION_PPT,         application/vnd.ms-powerpoint,      ppt)    \
    XX(APPLICATION_EOT,         application/vnd.ms-fontobject,      eot)    \
    XX(APPLICATION_M3U8,        application/vnd.apple.mpegurl,      m3u8)   \
    XX(APPLICATION_DOCX,        application/vnd.openxmlformats-officedocument.wordprocessingml.document,    docx) \
    XX(APPLICATION_XLSX,        application/vnd.openxmlformats-officedocument.spreadsheetml.sheet,          xlsx) \
    XX(APPLICATION_PPTX,        application/vnd.openxmlformats-officedocument.presentationml.presentation,  pptx) \

#define MIME_TYPE_MULTIPART_MAP(XX) \
    XX(MULTIPART_FORM_DATA,     multipart/form-data,                mp) \

#define MIME_TYPE_IMAGE_MAP(XX) \
    XX(IMAGE_JPEG,              image/jpeg,               jpg)          \
    XX(IMAGE_PNG,               image/png,                png)          \
    XX(IMAGE_GIF,               image/gif,                gif)          \
    XX(IMAGE_ICO,               image/x-icon,             ico)          \
    XX(IMAGE_BMP,               image/x-ms-bmp,           bmp)          \
    XX(IMAGE_SVG,               image/svg+xml,            svg)          \
    XX(IMAGE_TIFF,              image/tiff,               tiff)         \
    XX(IMAGE_WEBP,              image/webp,               webp)         \

#define MIME_TYPE_VIDEO_MAP(XX) \
    XX(VIDEO_MP4,               video/mp4,                mp4)          \
    XX(VIDEO_FLV,               video/x-flv,              flv)          \
    XX(VIDEO_M4V,               video/x-m4v,              m4v)          \
    XX(VIDEO_MNG,               video/x-mng,              mng)          \
    XX(VIDEO_TS,                video/mp2t,               ts)           \
    XX(VIDEO_MPEG,              video/mpeg,               mpeg)         \
    XX(VIDEO_WEBM,              video/webm,               webm)         \
    XX(VIDEO_MOV,               video/quicktime,          mov)          \
    XX(VIDEO_3GPP,              video/3gpp,               3gpp)         \
    XX(VIDEO_AVI,               video/x-msvideo,          avi)          \
    XX(VIDEO_WMV,               video/x-ms-wmv,           wmv)          \
    XX(VIDEO_ASF,               video/x-ms-asf,           asf)          \

#define MIME_TYPE_AUDIO_MAP(XX) \
    XX(AUDIO_MP3,               audio/mpeg,               mp3)          \
    XX(AUDIO_OGG,               audio/ogg,                ogg)          \
    XX(AUDIO_M4A,               audio/x-m4a,              m4a)          \
    XX(AUDIO_AAC,               audio/aac,                aac)          \
    XX(AUDIO_PCMA,              audio/PCMA,               pcma)         \
    XX(AUDIO_OPUS,              audio/opus,               opus)         \

#define MIME_TYPE_FONT_MAP(XX) \
    XX(FONT_TTF,                font/ttf,                 ttf)          \
    XX(FONT_OTF,                font/otf,                 otf)          \
    XX(FONT_WOFF,               font/woff,                woff)         \
    XX(FONT_WOFF2,              font/woff2,               woff2)        \

#define HTTP_CONTENT_TYPE_MAP(XX)   \
    MIME_TYPE_TEXT_MAP(XX)          \
    MIME_TYPE_APPLICATION_MAP(XX)   \
    MIME_TYPE_MULTIPART_MAP(XX)     \
    MIME_TYPE_IMAGE_MAP(XX)         \
    MIME_TYPE_VIDEO_MAP(XX)         \
    MIME_TYPE_AUDIO_MAP(XX)         \
    MIME_TYPE_FONT_MAP(XX)          \

#define X_WWW_FORM_URLENCODED   APPLICATION_URLENCODED // for compatibility

enum http_content_type {
#define XX(name, string, suffix)   name,
    CONTENT_TYPE_NONE           = 0,

    CONTENT_TYPE_TEXT           = 100,
    MIME_TYPE_TEXT_MAP(XX)

    CONTENT_TYPE_APPLICATION    = 200,
    MIME_TYPE_APPLICATION_MAP(XX)

    CONTENT_TYPE_MULTIPART      = 300,
    MIME_TYPE_MULTIPART_MAP(XX)

    CONTENT_TYPE_IMAGE          = 400,
    MIME_TYPE_IMAGE_MAP(XX)

    CONTENT_TYPE_VIDEO          = 500,
    MIME_TYPE_VIDEO_MAP(XX)

    CONTENT_TYPE_AUDIO          = 600,
    MIME_TYPE_AUDIO_MAP(XX)

    CONTENT_TYPE_FONT           = 700,
    MIME_TYPE_FONT_MAP(XX)

    CONTENT_TYPE_UNDEFINED      = 1000
#undef XX
};

BEGIN_EXTERN_C

HV_EXPORT const char* http_status_str(enum http_status status);
HV_EXPORT const char* http_method_str(enum http_method method);
HV_EXPORT const char* http_content_type_str(enum http_content_type type);

HV_EXPORT enum http_status http_status_enum(const char* str);
HV_EXPORT enum http_method http_method_enum(const char* str);
HV_EXPORT enum http_content_type http_content_type_enum(const char* str);

HV_EXPORT const char* http_content_type_suffix(enum http_content_type type);
HV_EXPORT const char* http_content_type_str_by_suffix(const char* suffix);
HV_EXPORT enum http_content_type http_content_type_enum_by_suffix(const char* suffix);

END_EXTERN_C

#endif // HV_HTTP_DEF_H_
