#include <atomic>
#include <stdio.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "WebSocketServer.h"
#include "WebSocketClient.h"
#include "HttpServer.h"
#include "HttpClient.h"
#include "http_compress.h"
#include "hthread.h"
#include "wsdef.h"

using namespace hv;

namespace {

struct WebSocketTestState {
    std::mutex mutex;
    std::string request_extensions;
    std::string response_extensions;
    int server_received_count;
    int client_received_count;
    bool server_payload_mismatch;
    bool client_payload_mismatch;
    std::atomic<bool> opened;
    std::atomic<bool> received;
    std::atomic<bool> closed;

    WebSocketTestState()
        : server_received_count(0)
        , client_received_count(0)
        , server_payload_mismatch(false)
        , client_payload_mismatch(false)
        , opened(false)
        , received(false)
        , closed(false) {}
};

static bool Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "websocket_compression_test: %s\n", message);
        return false;
    }
    return true;
}

static bool WaitHttpReady(int port) {
    HttpClient client;
    client.setTimeout(1);
    for (int i = 0; i < 100; ++i) {
        HttpRequest req;
        HttpResponse resp;
        req.method = HTTP_GET;
        req.url = hv::asprintf("http://127.0.0.1:%d/health", port);
        int ret = client.send(&req, &resp);
        if (ret == 0 && resp.status_code == HTTP_STATUS_OK && resp.body == "ok") {
            return true;
        }
        hv_delay(20);
    }
    return false;
}

static std::string MakePayload() {
    std::string payload;
    payload.reserve(4096);
    for (int i = 0; i < 4096; ++i) {
        payload.push_back((char)('a' + (i % 26)));
    }
    return payload;
}

static std::string BuildFrame(const std::string& payload, enum ws_opcode opcode, bool fin, bool rsv1) {
    int frame_size = ws_calc_frame_size((int)payload.size(), false);
    std::string frame(frame_size, '\0');
    char mask[4] = {0};
    ws_build_frame_ex(&frame[0], payload.data(), payload.size(), mask, false, opcode, fin, rsv1);
    return frame;
}

static bool ParserRejectsFrame(const std::string& frame, const char* what) {
    WebSocketParser parser;
    bool delivered = false;
    parser.onMessage = [&delivered](int, const std::string&) {
        delivered = true;
    };
    int nparse = parser.FeedRecvData(frame.data(), frame.size());
    if (!Check(nparse != (int)frame.size(), what)) return false;
    if (!Check(!delivered, "invalid websocket frame should not reach onMessage")) return false;
    return true;
}

static unsigned NextDeterministic(unsigned* state) {
    *state = (*state * 1103515245u) + 12345u;
    return *state;
}

static std::string MutatePayloadDeterministically(const std::string& input, unsigned seed) {
    std::string mutated = input;
    if (mutated.empty()) {
        mutated.push_back((char)(seed & 0xFF));
        return mutated;
    }
    // Build a repeatable malformed corpus so failures can be reproduced
    // without needing an external fuzzing harness.
    unsigned state = seed | 1u;
    int operations = 1 + (int)(NextDeterministic(&state) % 4);
    for (int i = 0; i < operations; ++i) {
        unsigned action = NextDeterministic(&state) % 4;
        if (action == 0 || mutated.size() < 2) {
            size_t pos = NextDeterministic(&state) % mutated.size();
            mutated[pos] = (char)(mutated[pos] ^ (char)(1u << (NextDeterministic(&state) % 7)));
        } else if (action == 1) {
            size_t pos = NextDeterministic(&state) % mutated.size();
            mutated[pos] = (char)NextDeterministic(&state);
        } else if (action == 2 && mutated.size() > 1) {
            size_t shrink_limit = mutated.size() - 1;
            if (shrink_limit > 8) {
                shrink_limit = 8;
            }
            size_t shrink = 1 + (NextDeterministic(&state) % (unsigned)shrink_limit);
            mutated.resize(mutated.size() - shrink);
        } else {
            size_t append = 1 + (NextDeterministic(&state) % 4);
            for (size_t j = 0; j < append; ++j) {
                mutated.push_back((char)NextDeterministic(&state));
            }
        }
    }
    return mutated;
}

static bool TestNegotiationHelpers() {
    WebSocketCompressionOptions server_options;
    server_options.enabled = true;
    server_options.client_no_context_takeover = true;
    server_options.server_no_context_takeover = false;
    server_options.client_max_window_bits = 12;
    server_options.server_max_window_bits = 15;

    WebSocketCompressionHandshake negotiated;
    std::string response_header;
    bool ok = NegotiateWebSocketCompression(
            "permessage-deflate; client_no_context_takeover; client_max_window_bits; server_max_window_bits=10",
            server_options,
            &negotiated,
            &response_header);
    if (!Check(ok, "websocket negotiation helper failed")) return false;
    if (!Check(negotiated.client_no_context_takeover, "client_no_context_takeover should be negotiated when both sides request it")) return false;
    if (!Check(!negotiated.server_no_context_takeover, "server_no_context_takeover should not be negotiated unless offered by client")) return false;
    if (!Check(negotiated.client_max_window_bits == 12, "client_max_window_bits should respect local server limit")) return false;
    if (!Check(negotiated.server_max_window_bits == 10, "server_max_window_bits should respect client offer limit")) return false;
    if (!Check(response_header.find("client_max_window_bits=12") != std::string::npos, "response should include explicit client_max_window_bits value")) return false;
    if (!Check(response_header.find("server_max_window_bits=10") != std::string::npos, "response should include negotiated server_max_window_bits value")) return false;
    return true;
}

static bool TestDefaultCompressionOptions() {
    WebSocketCompressionOptions defaults;
    if (!Check(defaults.enabled, "default websocket compression options should enable permessage-deflate when zlib is available")) return false;
    if (!Check(!BuildWebSocketCompressionOffer(defaults).empty(), "default websocket compression options should generate an extension offer")) return false;
    return true;
}

static bool TestRejectUnexpectedNegotiationParams() {
    WebSocketCompressionHandshake negotiated;
    if (!Check(ConfirmWebSocketCompression("permessage-deflate; client_no_context_takeover",
                                           "permessage-deflate",
                                           &negotiated),
               "client should accept server-selected client_no_context_takeover")) return false;
    if (!Check(negotiated.client_no_context_takeover, "accepted client_no_context_takeover mismatch")) return false;
    if (!Check(ConfirmWebSocketCompression("permessage-deflate; server_no_context_takeover",
                                           "permessage-deflate",
                                           &negotiated),
               "client should accept server-selected server_no_context_takeover")) return false;
    if (!Check(negotiated.server_no_context_takeover, "accepted server_no_context_takeover mismatch")) return false;
    if (!Check(!ConfirmWebSocketCompression("permessage-deflate; server_max_window_bits=12",
                                            "permessage-deflate; server_max_window_bits=10",
                                            &negotiated),
               "client should reject server_max_window_bits above offered limit")) return false;
    if (!Check(!ConfirmWebSocketCompression("permessage-deflate; client_max_window_bits",
                                            "permessage-deflate; client_max_window_bits",
                                            &negotiated),
               "client should reject response client_max_window_bits without value")) return false;
    if (!Check(ConfirmWebSocketCompression("permessage-deflate; server_max_window_bits=10",
                                           "permessage-deflate; server_max_window_bits=10",
                                           &negotiated),
               "client should accept negotiated server_max_window_bits within offered limit")) return false;
    if (!Check(negotiated.server_max_window_bits == 10, "accepted server_max_window_bits mismatch")) return false;
    return true;
}

static bool TestOfferParsingAndResponseValidation() {
    WebSocketCompressionOptions server_options;
    server_options.enabled = true;
    server_options.client_no_context_takeover = false;
    server_options.server_no_context_takeover = true;
    server_options.client_max_window_bits = 15;
    server_options.server_max_window_bits = 15;

    WebSocketCompressionHandshake negotiated;
    std::string response_header;
    bool ok = NegotiateWebSocketCompression(
            "permessage-deflate; client_max_window_bits=15x, permessage-deflate; server_no_context_takeover",
            server_options,
            &negotiated,
            &response_header);
    if (!Check(!ok, "server should reject invalid permessage-deflate offer candidates")) return false;

    WebSocketCompressionOptions fallback_options = server_options;
    fallback_options.server_no_context_takeover = false;
    std::string request_header = "permessage-deflate; server_no_context_takeover, permessage-deflate";
    ok = NegotiateWebSocketCompression(request_header, fallback_options, &negotiated, &response_header);
    if (!Check(ok, "server should fall back to a later compatible permessage-deflate offer")) return false;
    if (!Check(response_header == "permessage-deflate", "fallback negotiation should select the plain permessage-deflate offer")) return false;
    if (!Check(ConfirmWebSocketCompression(response_header, request_header, &negotiated),
               "client should confirm response against the selected fallback offer")) return false;

    if (!Check(ConfirmWebSocketCompression("permessage-deflate; server_max_window_bits=12", "permessage-deflate", &negotiated),
               "client should accept server-selected server_max_window_bits even when not explicitly offered")) return false;
    if (!Check(negotiated.server_max_window_bits == 12, "accepted server-selected server_max_window_bits mismatch")) return false;

    WebSocketCompressionHandshake offer;
    if (!Check(ParseWebSocketCompressionExtensions("permessage-deflate; server_no_context_takeover", &offer),
               "failed to parse explicit websocket offer")) return false;
    if (!Check(!ConfirmWebSocketCompression("permessage-deflate", offer, &negotiated),
               "client should reject missing requested server_no_context_takeover in response")) return false;
    if (!Check(!ConfirmWebSocketCompression("permessage-deflate; server_no_context_takeover; client_max_window_bits=12", offer, &negotiated),
               "client should reject unrequested client_max_window_bits in response")) return false;
    if (!Check(!ConfirmWebSocketCompression("permessage-deflate, x-test", offer, &negotiated),
               "client should reject unexpected extension tokens in response")) return false;
    if (!Check(!ConfirmWebSocketCompression("permessage-deflate, permessage-deflate", offer, &negotiated),
               "client should reject duplicate permessage-deflate response extensions")) return false;
    if (!Check(ParseWebSocketCompressionExtensions("permessage-deflate; client_max_window_bits=\"12\"", &offer),
               "quoted-string websocket offer should parse")) return false;
    if (!Check(offer.client_max_window_bits_requested && offer.client_max_window_bits == 12,
               "quoted-string client_max_window_bits value mismatch")) return false;
    if (!Check(ConfirmWebSocketCompression("permessage-deflate; client_max_window_bits=\"12\"",
                                           "permessage-deflate; client_max_window_bits",
                                           &negotiated),
               "quoted-string websocket response should validate")) return false;
    if (!Check(!ParseWebSocketCompressionExtensions("permessage-deflate; server_max_window_bits=08", &offer),
               "leading-zero websocket window bits should be rejected")) return false;
    return true;
}

static bool TestRejectCorruptedCompressedMessage() {
    WebSocketCompressionOptions options;
    options.enabled = true;
    options.client_no_context_takeover = true;
    options.server_no_context_takeover = true;
    options.client_max_window_bits = 15;
    options.server_max_window_bits = 15;
    options.max_decoded_size = 4096;

    WebSocketDeflater deflater;
    if (!Check(deflater.Init(options.client_max_window_bits, options.client_no_context_takeover) == 0,
               "failed to initialize websocket deflater for corruption test")) return false;

    const std::string payload = "corrupted-frame-payload";
    std::string compressed;
    if (!Check(deflater.CompressMessage(payload.data(), payload.size(), compressed) == 0,
               "failed to compress websocket payload for corruption test")) return false;
    if (!Check(!compressed.empty(), "compressed corruption test payload should not be empty")) return false;
    compressed.resize(compressed.size() - 1);

    int frame_size = ws_calc_frame_size((int)compressed.size(), false);
    std::string frame(frame_size, '\0');
    char mask[4] = {0};
    ws_build_frame_ex(&frame[0], compressed.data(), compressed.size(), mask, false, WS_OPCODE_TEXT, true, true);

    WebSocketParser parser;
    parser.setCompression(options, true, false);
    bool delivered = false;
    parser.onMessage = [&delivered](int, const std::string&) {
        delivered = true;
    };

    int nparse = parser.FeedRecvData(frame.data(), frame.size());
    if (!Check(nparse != (int)frame.size(), "corrupted compressed frame should not parse successfully")) return false;
    if (!Check(!delivered, "corrupted compressed frame should not reach onMessage")) return false;
    return true;
}

static bool TestRejectOverLimitCompressedMessage() {
    WebSocketCompressionOptions options;
    options.enabled = true;
    options.client_no_context_takeover = true;
    options.server_no_context_takeover = true;
    options.client_max_window_bits = 15;
    options.server_max_window_bits = 15;
    options.max_decoded_size = 1024;

    WebSocketDeflater deflater;
    if (!Check(deflater.Init(options.client_max_window_bits, options.client_no_context_takeover) == 0,
               "failed to initialize websocket deflater for over-limit test")) return false;

    std::string payload(4096, 'o');
    std::string compressed;
    if (!Check(deflater.CompressMessage(payload.data(), payload.size(), compressed) == 0,
               "failed to compress websocket payload for over-limit test")) return false;

    std::string frame = BuildFrame(compressed, WS_OPCODE_TEXT, true, true);
    WebSocketParser parser;
    parser.setCompression(options, true, false);
    bool delivered = false;
    parser.onMessage = [&delivered](int, const std::string&) {
        delivered = true;
    };

    int nparse = parser.FeedRecvData(frame.data(), frame.size());
    if (!Check(nparse != (int)frame.size(), "over-limit compressed frame should be rejected")) return false;
    if (!Check(!delivered, "over-limit compressed frame should not reach onMessage")) return false;
    return true;
}

static bool TestMalformedCompressedFrameCorpus() {
    WebSocketCompressionOptions options;
    options.enabled = true;
    options.client_no_context_takeover = false;
    options.server_no_context_takeover = false;
    options.client_max_window_bits = 15;
    options.server_max_window_bits = 15;
    options.max_decoded_size = 8192;

    WebSocketDeflater deflater;
    if (!Check(deflater.Init(options.client_max_window_bits, options.client_no_context_takeover) == 0,
               "failed to initialize websocket deflater for malformed corpus")) return false;

    std::string compressed;
    const std::string payload = MakePayload();
    if (!Check(deflater.CompressMessage(payload.data(), payload.size(), compressed) == 0,
               "failed to prepare websocket malformed corpus seed")) return false;

    for (unsigned i = 0; i < 128; ++i) {
        std::string mutated_payload = MutatePayloadDeterministically(compressed, 0xBAD5EEDu + i * 29u);
        std::string frame = BuildFrame(mutated_payload, WS_OPCODE_TEXT, true, true);

        WebSocketParser parser;
        parser.setCompression(options, true, false);
        std::vector<std::pair<int, std::string> > delivered;
        parser.onMessage = [&delivered](int opcode, const std::string& msg) {
            delivered.push_back(std::make_pair(opcode, msg));
        };

        int nparse = parser.FeedRecvData(frame.data(), frame.size());
        if (!Check(nparse <= (int)frame.size(), "malformed websocket corpus parse returned invalid byte count")) return false;
        if (!Check(delivered.size() <= 1, "malformed websocket corpus should not deliver multiple messages")) return false;
        if (!delivered.empty()) {
            // Some mutated frames can still be accepted as valid data frames;
            // keep the assertion focused on safe bounded behavior.
            if (!Check(delivered[0].first == WS_OPCODE_TEXT, "malformed websocket corpus delivered unexpected opcode")) return false;
            if (!Check(delivered[0].second.size() <= options.max_decoded_size, "malformed websocket corpus exceeded decoded size limit")) return false;
        }
    }
    return true;
}

static bool TestRepeatedCompressedMessages() {
    for (int mode = 0; mode < 2; ++mode) {
        bool no_context_takeover = false;
        if (mode == 0) {
            no_context_takeover = true;
        }
        WebSocketCompressionOptions options;
        options.enabled = true;
        options.client_no_context_takeover = no_context_takeover;
        options.server_no_context_takeover = no_context_takeover;
        options.client_max_window_bits = 15;
        options.server_max_window_bits = 15;
        options.max_decoded_size = 1u << 20;

        // Cover both takeover modes because compression state reuse changes
        // the lifetime of zlib context across messages.
        WebSocketDeflater deflater;
        if (!Check(deflater.Init(options.client_max_window_bits, options.client_no_context_takeover) == 0,
                   "failed to initialize websocket deflater for repeated-message test")) return false;

        WebSocketParser parser;
        parser.setCompression(options, true, false);
        std::vector<std::pair<int, std::string> > delivered;
        parser.onMessage = [&delivered](int opcode, const std::string& msg) {
            delivered.push_back(std::make_pair(opcode, msg));
        };

        for (int i = 0; i < 256; ++i) {
            std::string expected = hv::asprintf("message-%02d:%s", i, std::string(256 + i * 7, (char)('a' + (i % 26))).c_str());
            std::string compressed;
            if (!Check(deflater.CompressMessage(expected.data(), expected.size(), compressed) == 0,
                       "failed to compress repeated websocket message")) return false;
            std::string frame = BuildFrame(compressed, WS_OPCODE_TEXT, true, true);
            int nparse = parser.FeedRecvData(frame.data(), frame.size());
            if (!Check(nparse == (int)frame.size(), "repeated compressed websocket frame should parse successfully")) return false;
        }

        if (!Check(delivered.size() == 256, "repeated compressed websocket messages should all be delivered")) return false;
        for (int i = 0; i < 256; ++i) {
            std::string expected = hv::asprintf("message-%02d:%s", i, std::string(256 + i * 7, (char)('a' + (i % 26))).c_str());
            if (!Check(delivered[i].first == WS_OPCODE_TEXT && delivered[i].second == expected,
                       "repeated compressed websocket message payload mismatch")) return false;
        }
    }
    return true;
}

static bool TestFragmentedCompressedMessageWithControlFrame() {
    WebSocketCompressionOptions options;
    options.enabled = true;
    options.client_no_context_takeover = true;
    options.server_no_context_takeover = true;
    options.client_max_window_bits = 15;
    options.server_max_window_bits = 15;

    WebSocketDeflater deflater;
    if (!Check(deflater.Init(options.client_max_window_bits, options.client_no_context_takeover) == 0,
               "failed to initialize websocket deflater for fragmented control-frame test")) return false;

    const std::string payload = MakePayload();
    std::string compressed;
    if (!Check(deflater.CompressMessage(payload.data(), payload.size(), compressed) == 0,
               "failed to compress websocket payload for fragmented control-frame test")) return false;
    if (!Check(compressed.size() > 8, "compressed websocket payload should be large enough to fragment")) return false;

    size_t split = compressed.size() / 2;
    std::string first = BuildFrame(compressed.substr(0, split), WS_OPCODE_TEXT, false, true);
    std::string ping = BuildFrame("ping", WS_OPCODE_PING, true, false);
    std::string second = BuildFrame(compressed.substr(split), WS_OPCODE_CONTINUE, true, false);

    WebSocketParser parser;
    parser.setCompression(options, true, false);
    std::vector<std::pair<int, std::string> > delivered;
    parser.onMessage = [&delivered](int opcode, const std::string& msg) {
        delivered.push_back(std::make_pair(opcode, msg));
    };

    int nparse = parser.FeedRecvData(first.data(), first.size());
    if (!Check(nparse == (int)first.size(), "first compressed fragment should parse successfully")) return false;
    nparse = parser.FeedRecvData(ping.data(), ping.size());
    if (!Check(nparse == (int)ping.size(), "interleaved ping frame should parse successfully")) return false;
    nparse = parser.FeedRecvData(second.data(), second.size());
    if (!Check(nparse == (int)second.size(), "final compressed fragment should parse successfully")) return false;
    if (!Check(delivered.size() == 2, "fragmented compressed message with interleaved control frame should deliver both control and data messages")) return false;
    if (!Check(delivered[0].first == WS_OPCODE_PING && delivered[0].second == "ping",
               "interleaved control frame should preserve ping opcode and payload")) return false;
    if (!Check(delivered[1].first == WS_OPCODE_TEXT && delivered[1].second == payload,
               "fragmented compressed message should preserve original opcode and decoded payload")) return false;
    return true;
}

static bool TestRejectInterleavedDataFrames() {
    std::string first = BuildFrame("part-one", WS_OPCODE_TEXT, false, false);
    std::string second = BuildFrame("part-two", WS_OPCODE_TEXT, true, false);

    WebSocketParser parser;
    bool delivered = false;
    parser.onMessage = [&delivered](int, const std::string&) {
        delivered = true;
    };

    int nparse = parser.FeedRecvData(first.data(), first.size());
    if (!Check(nparse == (int)first.size(), "first fragmented data frame should parse successfully")) return false;
    nparse = parser.FeedRecvData(second.data(), second.size());
    if (!Check(nparse != (int)second.size(), "interleaved data frame should be rejected")) return false;
    if (!Check(!delivered, "interleaved data frame should not produce a message")) return false;
    return true;
}

static bool TestRejectInvalidControlFrames() {
    std::string fragmented_ping = BuildFrame("ping", WS_OPCODE_PING, false, false);
    if (!ParserRejectsFrame(fragmented_ping, "fragmented control frame should be rejected")) return false;

    std::string oversized_payload(126, 'x');
    std::string oversized_ping = BuildFrame(oversized_payload, WS_OPCODE_PING, true, false);
    if (!ParserRejectsFrame(oversized_ping, "oversized control frame should be rejected")) return false;
    return true;
}

static bool TestRejectReservedBitsAndOpcodes() {
    std::string rsv2 = BuildFrame("hello", WS_OPCODE_TEXT, true, false);
    rsv2[0] = (char)(rsv2[0] | 0x20);
    if (!ParserRejectsFrame(rsv2, "RSV2 frame should be rejected")) return false;

    std::string rsv3 = BuildFrame("hello", WS_OPCODE_TEXT, true, false);
    rsv3[0] = (char)(rsv3[0] | 0x10);
    if (!ParserRejectsFrame(rsv3, "RSV3 frame should be rejected")) return false;

    std::string reserved_opcode = BuildFrame("hello", WS_OPCODE_TEXT, true, false);
    reserved_opcode[0] = (char)((reserved_opcode[0] & 0xF0) | 0x03);
    if (!ParserRejectsFrame(reserved_opcode, "reserved opcode frame should be rejected")) return false;
    return true;
}

} // namespace

int main() {
#if !defined(WITH_ZLIB)
    printf("websocket_compression_test skipped: requires WITH_ZLIB.\n");
    return 0;
#else
    WebSocketTestState state;
    const std::string payload = MakePayload();
    if (!TestDefaultCompressionOptions()) {
        return 1;
    }
    if (!TestNegotiationHelpers()) {
        return 1;
    }
    if (!TestRejectUnexpectedNegotiationParams()) {
        return 1;
    }
    if (!TestOfferParsingAndResponseValidation()) {
        return 1;
    }
    if (!TestRejectCorruptedCompressedMessage()) {
        return 1;
    }
    if (!TestRejectOverLimitCompressedMessage()) {
        return 1;
    }
    if (!TestMalformedCompressedFrameCorpus()) {
        return 1;
    }
    if (!TestRepeatedCompressedMessages()) {
        return 1;
    }
    if (!TestFragmentedCompressedMessageWithControlFrame()) {
        return 1;
    }
    if (!TestRejectInterleavedDataFrames()) {
        return 1;
    }
    if (!TestRejectInvalidControlFrames()) {
        return 1;
    }
    if (!TestRejectReservedBitsAndOpcodes()) {
        return 1;
    }

    HttpService http;
    http.GET("/health", [](HttpRequest*, HttpResponse* resp) {
        return resp->String("ok");
    });

    WebSocketService ws;
    WebSocketCompressionOptions ws_options;
    ws_options.enabled = true;
    ws_options.min_length = 1;
    ws_options.client_no_context_takeover = true;
    ws_options.server_no_context_takeover = true;
    ws_options.client_max_window_bits = 13;
    ws_options.server_max_window_bits = 11;
    ws.setCompression(ws_options);
    ws.onopen = [&state](const WebSocketChannelPtr&, const HttpRequestPtr& req) {
        std::lock_guard<std::mutex> locker(state.mutex);
        state.request_extensions = req->GetHeader(SEC_WEBSOCKET_EXTENSIONS);
    };
    ws.onmessage = [&state, &payload](const WebSocketChannelPtr& channel, const std::string& msg) {
        {
            std::lock_guard<std::mutex> locker(state.mutex);
            ++state.server_received_count;
            if (msg != payload) {
                state.server_payload_mismatch = true;
            }
        }
        channel->send(msg, channel->opcode);
    };

    WebSocketServer server(&ws);
    server.registerHttpService(&http);
    int port = 22000 + (hv_getpid() % 1000);
    server.setPort(port);
    if (!Check(server.start() == 0, "failed to start WebSocket test server")) {
        return 1;
    }
    if (!Check(WaitHttpReady(port), "WebSocket test server not ready")) {
        server.stop();
        return 1;
    }

    WebSocketClient client;
    WebSocketCompressionOptions client_options;
    client_options.enabled = true;
    client_options.min_length = 1;
    client_options.client_no_context_takeover = true;
    client_options.server_no_context_takeover = true;
    client_options.client_max_window_bits = 12;
    client_options.server_max_window_bits = 10;
    client.setCompression(client_options);
    client.setPingInterval(0);

    const int burst_count = 64;
    client.onopen = [&]() {
        const HttpResponsePtr& resp = client.getHttpResponse();
        {
            std::lock_guard<std::mutex> locker(state.mutex);
            state.response_extensions.clear();
            if (resp != NULL) {
                state.response_extensions = resp->GetHeader(SEC_WEBSOCKET_EXTENSIONS);
            }
        }
        state.opened = true;
        client.send(payload);
    };
    client.onmessage = [&](const std::string& msg) {
        int received_count = 0;
        {
            std::lock_guard<std::mutex> locker(state.mutex);
            ++state.client_received_count;
            received_count = state.client_received_count;
            if (msg != payload) {
                state.client_payload_mismatch = true;
            }
        }
        if (received_count >= burst_count) {
            state.received = true;
            client.close();
        } else {
            client.send(payload);
        }
    };
    client.onclose = [&]() {
        state.closed = true;
    };

    std::string url = hv::asprintf("ws://127.0.0.1:%d/ws", port);
    if (!Check(client.open(url.c_str()) == 0, "failed to open WebSocket client")) {
        server.stop();
        return 1;
    }

    for (int i = 0; i < 500 && !state.received.load(); ++i) {
        hv_delay(10);
    }

    client.stop();
    server.stop();
    hv_delay(50);

    if (!Check(state.opened.load(), "WebSocket connection was not opened")) return 1;
    if (!Check(state.received.load(), "WebSocket echo message was not received")) return 1;

    {
        std::lock_guard<std::mutex> locker(state.mutex);
        if (!Check(state.request_extensions.find("permessage-deflate") != std::string::npos, "client did not advertise permessage-deflate")) return 1;
        if (!Check(state.response_extensions.find("permessage-deflate") != std::string::npos, "server did not negotiate permessage-deflate")) return 1;
        if (!Check(state.request_extensions.find("client_max_window_bits=12") != std::string::npos, "client offer missing client_max_window_bits")) return 1;
        if (!Check(state.request_extensions.find("server_max_window_bits=10") != std::string::npos, "client offer missing server_max_window_bits")) return 1;
        if (!Check(state.response_extensions.find("client_no_context_takeover") != std::string::npos, "server response missing client_no_context_takeover")) return 1;
        if (!Check(state.response_extensions.find("server_no_context_takeover") != std::string::npos, "server response missing server_no_context_takeover")) return 1;
        if (!Check(state.response_extensions.find("client_max_window_bits=12") != std::string::npos, "server response missing negotiated client_max_window_bits")) return 1;
        if (!Check(state.response_extensions.find("server_max_window_bits=10") != std::string::npos, "server response missing negotiated server_max_window_bits")) return 1;
        if (!Check(state.server_received_count == burst_count, "server should receive the full compressed WebSocket burst")) return 1;
        if (!Check(state.client_received_count == burst_count, "client should receive the full compressed WebSocket burst")) return 1;
        if (!Check(!state.server_payload_mismatch, "server observed unexpected compressed WebSocket payload")) return 1;
        if (!Check(!state.client_payload_mismatch, "client observed unexpected compressed WebSocket payload")) return 1;
    }

    printf("websocket_compression_test passed.\n");
    return 0;
#endif
}
