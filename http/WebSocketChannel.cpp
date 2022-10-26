#include "WebSocketChannel.h"

namespace hv {

int WebSocketChannel::send(const char* buf, int len, enum ws_opcode opcode /* = WS_OPCODE_BINARY */, bool fin /* = true */) {
    int fragment = 0xFFFF; // 65535
    if (len > fragment) {
        return send(buf, len, fragment, opcode);
    }
    std::lock_guard<std::mutex> locker(mutex_);
    return sendFrame(buf, len, opcode, fin);
}

/*
 * websocket fragment
 * lock ->
 * sendFrame(p, fragment, opcode, false) ->
 * sendFrame(p, fragment, WS_OPCODE_CONTINUE, false) ->
 * ... ->
 * sendFrame(p, remain, WS_OPCODE_CONTINUE, true)
 * unlock
 *
 */
int WebSocketChannel::send(const char* buf, int len, int fragment, enum ws_opcode opcode /* = WS_OPCODE_BINARY */) {
    std::lock_guard<std::mutex> locker(mutex_);
    if (len <= fragment) {
        return sendFrame(buf, len, opcode, true);
    }

    // first fragment
    int nsend = sendFrame(buf, fragment, opcode, false);
    if (nsend < 0) return nsend;

    const char* p = buf + fragment;
    int remain = len - fragment;
    while (remain > fragment) {
        nsend = sendFrame(p, fragment, WS_OPCODE_CONTINUE, false);
        if (nsend < 0) return nsend;
        p += fragment;
        remain -= fragment;
    }

    // last fragment
    nsend = sendFrame(p, remain, WS_OPCODE_CONTINUE, true);
    if (nsend < 0) return nsend;

    return len;
}

int WebSocketChannel::sendPing() {
    std::lock_guard<std::mutex> locker(mutex_);
    if (type == WS_CLIENT) {
        return write(WS_CLIENT_PING_FRAME, WS_CLIENT_MIN_FRAME_SIZE);
    }
    return write(WS_SERVER_PING_FRAME, WS_SERVER_MIN_FRAME_SIZE);
}

int WebSocketChannel::sendPong() {
    std::lock_guard<std::mutex> locker(mutex_);
    if (type == WS_CLIENT) {
        return write(WS_CLIENT_PONG_FRAME, WS_CLIENT_MIN_FRAME_SIZE);
    }
    return write(WS_SERVER_PONG_FRAME, WS_SERVER_MIN_FRAME_SIZE);
}

int WebSocketChannel::sendFrame(const char* buf, int len, enum ws_opcode opcode /* = WS_OPCODE_BINARY */, bool fin /* = true */) {
    bool has_mask = false;
    char mask[4] = {0};
    if (type == WS_CLIENT) {
        *(int*)mask = rand();
        has_mask = true;
    }
    int frame_size = ws_calc_frame_size(len, has_mask);
    if (sendbuf_.len < (size_t)frame_size) {
        sendbuf_.resize(ceil2e(frame_size));
    }
    ws_build_frame(sendbuf_.base, buf, len, mask, has_mask, opcode, fin);
    return write(sendbuf_.base, frame_size);
}

}
