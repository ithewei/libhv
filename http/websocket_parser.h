#ifndef WEBSOCKET_PARSER_H
#define WEBSOCKET_PARSER_H
#ifdef __cplusplus
extern "C" {
#endif


#include <sys/types.h>
#if defined(_WIN32) && !defined(__MINGW32__) && \
  (!defined(_MSC_VER) || _MSC_VER<1600) && !defined(__WINE__)
#include <BaseTsd.h>
#include <stddef.h>
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

#define WEBSOCKET_UUID   "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct websocket_parser websocket_parser;
typedef struct websocket_parser_settings websocket_parser_settings;

typedef enum websocket_flags {
    // opcodes
    WS_OP_CONTINUE = 0x0,
    WS_OP_TEXT     = 0x1,
    WS_OP_BINARY   = 0x2,
    WS_OP_CLOSE    = 0x8,
    WS_OP_PING     = 0x9,
    WS_OP_PONG     = 0xA,

    // marks
    WS_FINAL_FRAME = 0x10,
    WS_HAS_MASK    = 0x20,
} websocket_flags;

#define WS_OP_MASK 0xF
#define WS_FIN     WS_FINAL_FRAME

typedef int (*websocket_data_cb) (websocket_parser*, const char * at, size_t length);
typedef int (*websocket_cb) (websocket_parser*);

struct websocket_parser {
    uint32_t        state;
    websocket_flags flags;

    char            mask[4];
    uint8_t         mask_offset;

    size_t   length;
    size_t   require;
    size_t   offset;

    void * data;
};

struct websocket_parser_settings {
    websocket_cb      on_frame_header;
    websocket_data_cb on_frame_body;
    websocket_cb      on_frame_end;
};

void websocket_parser_init(websocket_parser *parser);
void websocket_parser_settings_init(websocket_parser_settings *settings);
size_t websocket_parser_execute(
    websocket_parser * parser,
    const websocket_parser_settings *settings,
    const char * data,
    size_t len
);

// Apply XOR mask (see https://tools.ietf.org/html/rfc6455#section-5.3) and store mask's offset
void websocket_parser_decode(char * dst, const char * src, size_t len, websocket_parser * parser);

// Apply XOR mask (see https://tools.ietf.org/html/rfc6455#section-5.3) and return mask's offset
uint8_t websocket_decode(char * dst, const char * src, size_t len, const char mask[4], uint8_t mask_offset);
#define websocket_encode(dst, src, len, mask, mask_offset) websocket_decode(dst, src, len, mask, mask_offset)

// Calculate frame size using flags and data length
size_t websocket_calc_frame_size(websocket_flags flags, size_t data_len);

// Create string representation of frame
size_t websocket_build_frame(char * frame, websocket_flags flags, const char mask[4], const char * data, size_t data_len);

#define websocket_parser_get_opcode(p) (p->flags & WS_OP_MASK)
#define websocket_parser_has_mask(p) (p->flags & WS_HAS_MASK)
#define websocket_parser_has_final(p) (p->flags & WS_FIN)

#ifdef __cplusplus
}
#endif
#endif //WEBSOCKET_PARSER_H
