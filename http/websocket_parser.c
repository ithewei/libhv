#include "websocket_parser.h"
#include <assert.h>
#include <string.h>

#ifdef assert
# define assertFalse(msg) assert(0 && msg)
#else
# define assertFalse(msg)
#endif

#define SET_STATE(V) parser->state = V
#define HAS_DATA() (p < end )
#define CC (*p)
#define GET_NPARSED() ( (p == end) ? len : (p - data) )

#define NOTIFY_CB(FOR)                                                 \
do {                                                                   \
  if (settings->on_##FOR) {                                            \
    if (settings->on_##FOR(parser) != 0) {                             \
      return GET_NPARSED();                                            \
    }                                                                  \
  }                                                                    \
} while (0)

#define EMIT_DATA_CB(FOR, ptr, len)                                    \
do {                                                                   \
  if (settings->on_##FOR) {                                            \
    if (settings->on_##FOR(parser, ptr, len) != 0) {                   \
      return GET_NPARSED();                                            \
    }                                                                  \
  }                                                                    \
} while (0)

enum state {
    s_start,
    s_head,
    s_length,
    s_mask,
    s_body,
};

void websocket_parser_init(websocket_parser * parser) {
    void *data = parser->data; /* preserve application data */
    memset(parser, 0, sizeof(*parser));
    parser->data = data;
    parser->state = s_start;
}

void websocket_parser_settings_init(websocket_parser_settings *settings) {
    memset(settings, 0, sizeof(*settings));
}

size_t websocket_parser_execute(websocket_parser *parser, const websocket_parser_settings *settings, const char *data, size_t len) {
    const char * p;
    const char * end = data + len;
    size_t frame_offset = 0;

    for(p = data; p != end; p++) {
        switch(parser->state) {
            case s_start:
                parser->offset      = 0;
                parser->length      = 0;
                parser->mask_offset = 0;
                parser->flags       = (websocket_flags) (CC & WS_OP_MASK);
                if(CC & (1<<7)) {
                    parser->flags |= WS_FIN;
                }
                SET_STATE(s_head);

                frame_offset++;
                break;
            case s_head:
                parser->length  = (size_t)CC & 0x7F;
                if(CC & 0x80) {
                    parser->flags |= WS_HAS_MASK;
                }
                if(parser->length >= 126) {
                    if(parser->length == 127) {
                        parser->require = 8;
                    } else {
                        parser->require = 2;
                    }
                    parser->length = 0;
                    SET_STATE(s_length);
                } else if (parser->flags & WS_HAS_MASK) {
                    SET_STATE(s_mask);
                    parser->require = 4;
                } else if (parser->length) {
                    SET_STATE(s_body);
                    parser->require = parser->length;
                    NOTIFY_CB(frame_header);
                } else {
                    SET_STATE(s_start);
                    NOTIFY_CB(frame_header);
                    NOTIFY_CB(frame_end);
                }

                frame_offset++;
                break;
            case s_length:
                while(HAS_DATA() && parser->require) {
                    parser->length <<= 8;
                    parser->length |= (unsigned char)CC;
                    parser->require--;
                    frame_offset++;
                    p++;
                }
                p--;
                if(!parser->require) {
                    if (parser->flags & WS_HAS_MASK) {
                        SET_STATE(s_mask);
                        parser->require = 4;
                    } else if (parser->length) {
                        SET_STATE(s_body);
                        parser->require = parser->length;
                        NOTIFY_CB(frame_header);
                    } else {
                        SET_STATE(s_start);
                        NOTIFY_CB(frame_header);
                        NOTIFY_CB(frame_end);
                    }
                }
                break;
            case s_mask:
                while(HAS_DATA() && parser->require) {
                    parser->mask[4 - parser->require--] = CC;
                    frame_offset++;
                    p++;
                }
                p--;
                if(!parser->require) {
                    if(parser->length) {
                        SET_STATE(s_body);
                        parser->require = parser->length;
                        NOTIFY_CB(frame_header);
                    } else {
                        SET_STATE(s_start);
                        NOTIFY_CB(frame_header);
                        NOTIFY_CB(frame_end);
                    }
                }
                break;
            case s_body:
                if(parser->require) {
                    if(p + parser->require <= end) {
                        EMIT_DATA_CB(frame_body, p, parser->require);
                        p += parser->require;
                        parser->require = 0;
                        frame_offset = p - data;
                    } else {
                        EMIT_DATA_CB(frame_body, p, end - p);
                        parser->require -= end - p;
                        p = end;
                        parser->offset += p - data - frame_offset;
                        frame_offset = 0;
                    }

                    p--;
                }
                if(!parser->require) {
                    SET_STATE(s_start);
                    NOTIFY_CB(frame_end);
                }
                break;
            default:
                assertFalse("Unreachable case");
        }
    }

    return GET_NPARSED();
}

void websocket_parser_decode(char * dst, const char * src, size_t len, websocket_parser * parser) {
    size_t i = 0;
    for(; i < len; i++) {
        dst[i] = src[i] ^ parser->mask[(i + parser->mask_offset) % 4];
    }

    parser->mask_offset = (uint8_t) ((i + parser->mask_offset) % 4);
}

uint8_t websocket_decode(char * dst, const char * src, size_t len, const char mask[4], uint8_t mask_offset) {
    size_t i = 0;
    for(; i < len; i++) {
        dst[i] = src[i] ^ mask[(i + mask_offset) % 4];
    }

    return (uint8_t) ((i + mask_offset) % 4);
}

size_t websocket_calc_frame_size(websocket_flags flags, size_t data_len) {
    size_t size = data_len + 2; // body + 2 bytes of head
    if(data_len >= 126) {
        if(data_len > 0xFFFF) {
            size += 8;
        } else {
            size += 2;
        }
    }
    if(flags & WS_HAS_MASK) {
        size += 4;
    }

    return size;
}

size_t websocket_build_frame(char * frame, websocket_flags flags, const char mask[4], const char * data, size_t data_len) {
    size_t body_offset = 0;
    frame[0] = 0;
    frame[1] = 0;
    if(flags & WS_FIN) {
        frame[0] = (char) (1 << 7);
    }
    frame[0] |= flags & WS_OP_MASK;
    if(flags & WS_HAS_MASK) {
        frame[1] = (char) (1 << 7);
    }
    if(data_len < 126) {
        frame[1] |= data_len;
        body_offset = 2;
    } else if(data_len <= 0xFFFF) {
        frame[1] |= 126;
        frame[2] = (char) (data_len >> 8);
        frame[3] = (char) (data_len & 0xFF);
        body_offset = 4;
    } else {
        frame[1] |= 127;
        frame[2] = (char) ((data_len >> 56) & 0xFF);
        frame[3] = (char) ((data_len >> 48) & 0xFF);
        frame[4] = (char) ((data_len >> 40) & 0xFF);
        frame[5] = (char) ((data_len >> 32) & 0xFF);
        frame[6] = (char) ((data_len >> 24) & 0xFF);
        frame[7] = (char) ((data_len >> 16) & 0xFF);
        frame[8] = (char) ((data_len >>  8) & 0xFF);
        frame[9] = (char) ((data_len)       & 0xFF);
        body_offset = 10;
    }
    if(flags & WS_HAS_MASK) {
        if(mask != NULL) {
            memcpy(&frame[body_offset], mask, 4);
        }
        websocket_decode(&frame[body_offset + 4], data, data_len, &frame[body_offset], 0);
        body_offset += 4;
    } else {
        memcpy(&frame[body_offset], data, data_len);
    }

    return body_offset + data_len;
}
