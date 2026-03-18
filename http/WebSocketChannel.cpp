#include "WebSocketChannel.h"
#include "http_compress.h"

namespace hv {

static bool ws_is_compressible_opcode(enum ws_opcode opcode) {
    return opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY;
}

static bool ws_should_compress(const WebSocketCompressionOptions& options, enum ws_opcode opcode, bool fin, int len) {
    return options.enabled &&
           WebSocketCompressionAvailable() &&
           ws_is_compressible_opcode(opcode) &&
           fin &&
           len >= (int)options.min_length;
}

int WebSocketChannel::send(const char* buf, int len, enum ws_opcode opcode /* = WS_OPCODE_BINARY */, bool fin /* = true */) {
    if (ws_should_compress(compression, opcode, fin, len)) {
        std::lock_guard<std::mutex> locker(mutex_);
        if (!deflater_) {
            deflater_ = std::make_shared<WebSocketDeflater>();
        }
        int window_bits = type == WS_CLIENT ? compression.client_max_window_bits : compression.server_max_window_bits;
        bool no_context_takeover = type == WS_CLIENT ? compression.client_no_context_takeover : compression.server_no_context_takeover;
        if (!deflater_->ready()) {
            int ret = deflater_->Init(window_bits, no_context_takeover);
            if (ret != 0) {
                return ret;
            }
        }
        std::string compressed;
        int ret = deflater_->CompressMessage(buf, len, compressed);
        if (ret != 0) {
            return ret;
        }
        int fragment = 0xFFFF;
        if ((int)compressed.size() > fragment) {
            int nsend = sendFrame(compressed.data(), fragment, opcode, false, true);
            if (nsend < 0) return nsend;
            const char* p = compressed.data() + fragment;
            int remain = (int)compressed.size() - fragment;
            while (remain > fragment) {
                nsend = sendFrame(p, fragment, WS_OPCODE_CONTINUE, false, false);
                if (nsend < 0) return nsend;
                p += fragment;
                remain -= fragment;
            }
            nsend = sendFrame(p, remain, WS_OPCODE_CONTINUE, true, false);
            if (nsend < 0) return nsend;
            return compressed.size();
        }
        return sendFrame(compressed.data(), compressed.size(), opcode, fin, true);
    }
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
    std::string compressed;
    bool use_compression = ws_should_compress(compression, opcode, true, len);
    std::lock_guard<std::mutex> locker(mutex_);
    if (use_compression) {
        if (!deflater_) {
            deflater_ = std::make_shared<WebSocketDeflater>();
        }
        int window_bits = type == WS_CLIENT ? compression.client_max_window_bits : compression.server_max_window_bits;
        bool no_context_takeover = type == WS_CLIENT ? compression.client_no_context_takeover : compression.server_no_context_takeover;
        if (!deflater_->ready()) {
            int ret = deflater_->Init(window_bits, no_context_takeover);
            if (ret != 0) {
                return ret;
            }
        }
        int ret = deflater_->CompressMessage(buf, len, compressed);
        if (ret != 0) {
            return ret;
        }
        buf = compressed.data();
        len = (int)compressed.size();
    }

    if (len <= fragment) {
        return sendFrame(buf, len, opcode, true, use_compression);
    }

    // first fragment
    int nsend = sendFrame(buf, fragment, opcode, false, use_compression);
    if (nsend < 0) return nsend;

    const char* p = buf + fragment;
    int remain = len - fragment;
    while (remain > fragment) {
        nsend = sendFrame(p, fragment, WS_OPCODE_CONTINUE, false, false);
        if (nsend < 0) return nsend;
        p += fragment;
        remain -= fragment;
    }

    // last fragment
    nsend = sendFrame(p, remain, WS_OPCODE_CONTINUE, true, false);
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

int WebSocketChannel::sendFrame(const char* buf, int len, enum ws_opcode opcode /* = WS_OPCODE_BINARY */, bool fin /* = true */, bool rsv1 /* = false */) {
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
    ws_build_frame_ex(sendbuf_.base, buf, len, mask, has_mask, opcode, fin, rsv1);
    return write(sendbuf_.base, frame_size);
}

}
