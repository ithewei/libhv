// Keep assertions active in Release builds: the checks also feed the parser.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

#include <string>
#include <utility>
#include <vector>

#include "WebSocketParser.h"
#include "wsdef.h"

typedef std::pair<int, std::string> Message;
typedef std::vector<Message> Messages;

static std::string frame(enum ws_opcode opcode, const std::string& payload, bool fin = true, bool has_mask = true) {
    static const char mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string buf(ws_calc_frame_size((int)payload.size(), has_mask), '\0');
    ws_build_frame(&buf[0], payload.data(), (int)payload.size(), mask, has_mask, opcode, fin);
    return buf;
}

static size_t feed(const std::string& data, Messages* messages, bool byte_by_byte = false, bool is_server = false) {
    WebSocketParser parser;
    assert(!parser.is_server);
    parser.is_server = is_server;
    parser.onMessage = [messages](int opcode, const std::string& msg) {
        messages->push_back(Message(opcode, msg));
    };
    if (!byte_by_byte) {
        return (size_t)parser.FeedRecvData(data.data(), data.size());
    }
    size_t nfeed = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (parser.FeedRecvData(data.data() + i, 1) != 1) break;
        ++nfeed;
    }
    return nfeed;
}

static void test_ping_between_fragments() {
    std::string data = frame(WS_OPCODE_TEXT, "Hel", false) +
                       frame(WS_OPCODE_PING, "x") +
                       frame(WS_OPCODE_CONTINUE, "lo");
    Messages messages;
    assert(feed(data, &messages) == data.size());
    assert(messages.size() == 2);
    assert(messages[0] == Message(WS_OPCODE_PING, "x"));
    assert(messages[1] == Message(WS_OPCODE_TEXT, "Hello"));
}

static void test_control_frames_between_fragments_byte_by_byte() {
    std::string data = frame(WS_OPCODE_BINARY, "ab", false, false) +
                       frame(WS_OPCODE_PONG, "", true, false) +
                       frame(WS_OPCODE_CONTINUE, "cd", false, false) +
                       frame(WS_OPCODE_PING, "ping", true, false) +
                       frame(WS_OPCODE_CONTINUE, "ef", true, false);
    Messages messages;
    assert(feed(data, &messages, true) == data.size());
    assert(messages.size() == 3);
    assert(messages[0] == Message(WS_OPCODE_PONG, ""));
    assert(messages[1] == Message(WS_OPCODE_PING, "ping"));
    assert(messages[2] == Message(WS_OPCODE_BINARY, "abcdef"));
}

static void test_control_frame_between_messages() {
    std::string data = frame(WS_OPCODE_TEXT, "a") +
                       frame(WS_OPCODE_PING, "p") +
                       frame(WS_OPCODE_TEXT, "b", false) +
                       frame(WS_OPCODE_CONTINUE, "c");
    Messages messages;
    assert(feed(data, &messages) == data.size());
    assert(messages.size() == 3);
    assert(messages[0] == Message(WS_OPCODE_TEXT, "a"));
    assert(messages[1] == Message(WS_OPCODE_PING, "p"));
    assert(messages[2] == Message(WS_OPCODE_TEXT, "bc"));
}

static void test_max_control_payload() {
    std::string payload(125, 'A');
    std::string data = frame(WS_OPCODE_PING, payload);
    Messages messages;
    assert(feed(data, &messages) == data.size());
    assert(messages.size() == 1);
    assert(messages[0] == Message(WS_OPCODE_PING, payload));
}

static void test_reject_fragmented_control_frame() {
    std::string data = frame(WS_OPCODE_PING, "pi", false) +
                       frame(WS_OPCODE_CONTINUE, "ng");
    Messages messages;
    assert(feed(data, &messages) < data.size());
    assert(messages.empty());
}

static void test_reject_oversized_control_frame() {
    std::string data = frame(WS_OPCODE_PING, std::string(126, 'A'));
    Messages messages;
    assert(feed(data, &messages) < data.size());
    assert(messages.empty());
}

static void test_reject_reserved_bits() {
    for (int rsv = 0x10; rsv <= 0x70; rsv += 0x10) {
        for (int server = 0; server < 2; ++server) {
            for (int split = 0; split < 2; ++split) {
                std::string data = frame(WS_OPCODE_TEXT, "hi");
                data[0] |= (char)rsv;
                Messages messages;
                assert(feed(data, &messages, split != 0, server != 0) < data.size());
                assert(messages.empty());
            }
        }
    }
}

static void test_reject_reserved_opcodes() {
    for (int opcode = 0; opcode <= 0xF; ++opcode) {
        if (opcode <= 0x2 || (opcode >= 0x8 && opcode <= 0xA)) continue;
        for (int server = 0; server < 2; ++server) {
            for (int split = 0; split < 2; ++split) {
                std::string data = frame(WS_OPCODE_TEXT, "zz");
                data[0] = (char)(0x80 | opcode);
                Messages messages;
                assert(feed(data, &messages, split != 0, server != 0) < data.size());
                assert(messages.empty());
            }
        }
    }
}

static void test_server_mask_direction() {
    const enum ws_opcode opcodes[] = {WS_OPCODE_TEXT, WS_OPCODE_BINARY, WS_OPCODE_CLOSE, WS_OPCODE_PING, WS_OPCODE_PONG};
    for (size_t i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); ++i) {
        for (int empty = 0; empty < 2; ++empty) {
            for (int split = 0; split < 2; ++split) {
                std::string data = frame(opcodes[i], empty ? "" : "hi", true, false);
                Messages messages;
                assert(feed(data, &messages, split != 0, true) < data.size());
                assert(messages.empty());
            }
        }
    }
}

static void test_valid_mask_direction() {
    for (int server = 0; server < 2; ++server) {
        for (int masked = 0; masked < 2; ++masked) {
            if (server && !masked) continue;
            for (int split = 0; split < 2; ++split) {
                std::string data = frame(WS_OPCODE_TEXT, "Hel", false, masked != 0) + frame(WS_OPCODE_PING, "", true, masked != 0) +
                                   frame(WS_OPCODE_CONTINUE, "lo", true, masked != 0);
                Messages messages;
                assert(feed(data, &messages, split != 0, server != 0) == data.size());
                assert(messages.size() == 2);
                assert(messages[0] == Message(WS_OPCODE_PING, ""));
                assert(messages[1] == Message(WS_OPCODE_TEXT, "Hello"));
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc == 2) {
        std::string name = argv[1];
        if (name == "rsv") {
            test_reject_reserved_bits();
        }
        else if (name == "opcode") {
            test_reject_reserved_opcodes();
        }
        else if (name == "mask") {
            test_server_mask_direction();
        }
        else if (name == "valid") {
            test_valid_mask_direction();
        }
        else {
            return 1;
        }
        printf("PASS: %s\n", argv[1]);
        return 0;
    }
    test_ping_between_fragments();
    test_control_frames_between_fragments_byte_by_byte();
    test_control_frame_between_messages();
    test_max_control_payload();
    test_reject_fragmented_control_frame();
    test_reject_oversized_control_frame();
    test_reject_reserved_bits();
    test_reject_reserved_opcodes();
    test_server_mask_direction();
    test_valid_mask_direction();
    printf("PASS: 10 WebSocket parser tests\n");
    return 0;
}
