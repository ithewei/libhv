#ifndef HV_WS_DEF_H_
#define HV_WS_DEF_H_

#include "hexport.h"

#include <stdbool.h>
#include <stdlib.h> // import rand

#define SEC_WEBSOCKET_VERSION   "Sec-WebSocket-Version"
#define SEC_WEBSOCKET_KEY       "Sec-WebSocket-Key"
#define SEC_WEBSOCKET_ACCEPT    "Sec-WebSocket-Accept"

#define WS_SERVER_MIN_FRAME_SIZE    2
// 1000 1001 0000 0000
#define WS_SERVER_PING_FRAME        "\211\0"
// 1000 1010 0000 0000
#define WS_SERVER_PONG_FRAME        "\212\0"

#define WS_CLIENT_MIN_FRAME_SIZE    6
// 1000 1001 1000 0000
#define WS_CLIENT_PING_FRAME        "\211\200WSWS"
// 1000 1010 1000 0000
#define WS_CLIENT_PONG_FRAME        "\212\200WSWS"

enum ws_session_type {
    WS_CLIENT,
    WS_SERVER,
};

enum ws_opcode {
    WS_OPCODE_CONTINUE = 0x0,
    WS_OPCODE_TEXT     = 0x1,
    WS_OPCODE_BINARY   = 0x2,
    WS_OPCODE_CLOSE    = 0x8,
    WS_OPCODE_PING     = 0x9,
    WS_OPCODE_PONG     = 0xA,
};

BEGIN_EXTERN_C

// Sec-WebSocket-Key => Sec-WebSocket-Accept
HV_EXPORT void ws_encode_key(const char* key, char accept[]);

// fix-header[2] + var-length[2/8] + mask[4] + data[data_len]
HV_EXPORT int ws_calc_frame_size(int data_len, bool has_mask DEFAULT(false));

HV_EXPORT int ws_build_frame(
    char* out,
    const char* data,
    int data_len,
    const char mask[4],
    bool has_mask DEFAULT(false),
    enum ws_opcode opcode DEFAULT(WS_OPCODE_TEXT),
    bool fin DEFAULT(true));

HV_INLINE int ws_client_build_frame(
    char* out,
    const char* data,
    int data_len,
    /* const char mask[4] */
    /* bool has_mask = true */
    enum ws_opcode opcode DEFAULT(WS_OPCODE_TEXT),
    bool fin DEFAULT(true)) {
    char mask[4];
    *(int*)mask = rand();
    return ws_build_frame(out, data, data_len, mask, true, opcode, fin);
}

HV_INLINE int ws_server_build_frame(
    char* out,
    const char* data,
    int data_len,
    /* const char mask[4] */
    /* bool has_mask = false */
    enum ws_opcode opcode DEFAULT(WS_OPCODE_TEXT),
    bool fin DEFAULT(true)) {
    char mask[4] = {0};
    return ws_build_frame(out, data, data_len, mask, false, opcode, fin);
}

END_EXTERN_C

#endif // HV_WS_DEF_H_
