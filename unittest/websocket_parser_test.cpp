#include <assert.h>

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

static size_t feed(const std::string& data, Messages* messages, bool byte_by_byte = false) {
    WebSocketParser parser;
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

int main() {
    test_ping_between_fragments();
    test_control_frames_between_fragments_byte_by_byte();
    test_control_frame_between_messages();
    test_max_control_payload();
    test_reject_fragmented_control_frame();
    test_reject_oversized_control_frame();
    return 0;
}
