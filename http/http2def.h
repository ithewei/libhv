#ifndef HV_HTTP2_DEF_H_
#define HV_HTTP2_DEF_H_

#ifdef __cplusplus
extern "C" {
#endif


#define HTTP2_MAGIC             "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define HTTP2_MAGIC_LEN         24

// length:3bytes + type:1byte + flags:1byte + stream_id:4bytes = 9bytes
#define HTTP2_FRAME_HDLEN       9

#define HTTP2_UPGRADE_RESPONSE \
"HTTP/1.1 101 Switching Protocols\r\n"\
"Connection: Upgrade\r\n"\
"Upgrade: h2c\r\n\r\n"

typedef enum {
    HTTP2_DATA          = 0,
    HTTP2_HEADERS       = 0x01,
    HTTP2_PRIORITY      = 0x02,
    HTTP2_RST_STREAM    = 0x03,
    HTTP2_SETTINGS      = 0x04,
    HTTP2_PUSH_PROMISE  = 0x05,
    HTTP2_PING          = 0x06,
    HTTP2_GOAWAY        = 0x07,
    HTTP2_WINDOW_UPDATE = 0x08,
    HTTP2_CONTINUATION  = 0x09,
    HTTP2_ALTSVC        = 0x0a,
    HTTP2_ORIGIN        = 0x0c
} http2_frame_type;

typedef enum {
    HTTP2_FLAG_NONE         = 0,
    HTTP2_FLAG_END_STREAM   = 0x01,
    HTTP2_FLAG_END_HEADERS  = 0x04,
    HTTP2_FLAG_PADDED       = 0x08,
    HTTP2_FLAG_PRIORITY     = 0x20
} http2_flag;

typedef struct {
    int                 length;
    http2_frame_type    type;
    http2_flag          flags;
    int                 stream_id;
} http2_frame_hd;

static inline void http2_frame_hd_pack(const http2_frame_hd* hd, unsigned char* buf) {
    // hton
    int length = hd->length;
    int stream_id = hd->stream_id;
    unsigned char* p = buf;
    *p++ = (length >> 16) & 0xFF;
    *p++ = (length >>  8) & 0xFF;
    *p++ =  length        & 0xFF;
    *p++ = (unsigned char)hd->type;
    *p++ = (unsigned char)hd->flags;
    *p++ = (stream_id >> 24) & 0xFF;
    *p++ = (stream_id >> 16) & 0xFF;
    *p++ = (stream_id >>  8) & 0xFF;
    *p++ =  stream_id        & 0xFF;
}

static inline void http2_frame_hd_unpack(const unsigned char* buf, http2_frame_hd* hd) {
    // ntoh
    const unsigned char* p = buf;
    hd->length  = *p++ << 16;
    hd->length += *p++ << 8;
    hd->length += *p++;

    hd->type = (http2_frame_type)*p++;
    hd->flags = (http2_flag)*p++;

    hd->stream_id  = *p++ << 24;
    hd->stream_id += *p++ << 16;
    hd->stream_id += *p++ << 8;
    hd->stream_id += *p++;
}

#ifdef __cplusplus
}
#endif

#endif // HV_HTTP2_DEF_H_
