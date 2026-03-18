#include "WebSocketParser.h"

#include "websocket_parser.h"
#include "hdef.h"

#define MAX_PAYLOAD_LENGTH  ((size_t)1 << 24)   // 16M

static int on_frame_header(websocket_parser* parser) {
    WebSocketParser* wp = (WebSocketParser*)parser->data;
    int opcode = parser->flags & WS_OP_MASK;
    bool is_control = opcode == WS_OP_CLOSE || opcode == WS_OP_PING || opcode == WS_OP_PONG;
    bool is_data = opcode == WS_OP_CONTINUE || opcode == WS_OP_TEXT || opcode == WS_OP_BINARY;
    if (!is_control && !is_data) {
        return -1;
    }
    if (parser->flags & (WS_RSV2 | WS_RSV3)) {
        return -1;
    }
    // printf("on_frame_header opcode=%d\n", opcode);
    wp->opcode = opcode;
    wp->frame_is_control = is_control;
    wp->frame_message.clear();
    if (opcode != WS_OP_CONTINUE) {
        if (is_control) {
            if ((parser->flags & WS_FIN) == 0 ||
                parser->length > 125 ||
                (parser->flags & WS_RSV1)) {
                return -1;
            }
        } else {
            if (wp->fragmented_message) {
                return -1;
            }
            wp->message_opcode = opcode;
            wp->message_compressed = (parser->flags & WS_RSV1) != 0;
            wp->message.clear();
            wp->fragmented_message = (parser->flags & WS_FIN) == 0;
        }
    } else {
        if (parser->flags & WS_RSV1) {
            return -1;
        }
        if (!wp->fragmented_message) {
            return -1;
        }
        if (parser->flags & WS_FIN) {
            wp->fragmented_message = false;
        }
    }
    size_t reserve_length = parser->length >= MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : parser->length + 1;
    std::string* frame_buffer = wp->frame_is_control ? &wp->frame_message : &wp->message;
    if (reserve_length > frame_buffer->capacity()) {
        frame_buffer->reserve(reserve_length);
    }
    wp->state = WS_FRAME_HEADER;
    return 0;
}

static int on_frame_body(websocket_parser* parser, const char * at, size_t length) {
    // printf("on_frame_body length=%d\n", (int)length);
    WebSocketParser* wp = (WebSocketParser*)parser->data;
    wp->state = WS_FRAME_BODY;
    if (wp->parser->flags & WS_HAS_MASK) {
        websocket_parser_decode((char*)at, at, length, wp->parser);
    }
    if (wp->frame_is_control) {
        wp->frame_message.append(at, length);
    } else {
        wp->message.append(at, length);
    }
    return 0;
}

static int on_frame_end(websocket_parser* parser) {
    // printf("on_frame_end\n");
    WebSocketParser* wp = (WebSocketParser*)parser->data;
    wp->state = WS_FRAME_END;
    if (wp->parser->flags & WS_FIN) {
        wp->state = WS_FRAME_FIN;
        if (wp->onMessage) {
            if (wp->frame_is_control) {
                wp->onMessage(wp->opcode, wp->frame_message);
            } else if (wp->message_compressed) {
                if (!wp->compression_negotiated || !wp->inflater_) {
                    return -1;
                }
                std::string decoded;
                int ret = wp->inflater_->DecompressMessage(wp->message, decoded);
                if (ret != 0) {
                    return -1;
                }
                wp->onMessage(wp->message_opcode, decoded);
            } else {
                wp->onMessage(wp->message_opcode, wp->message);
            }
        }
    }
    return 0;
}

static websocket_parser_settings cbs = {
    on_frame_header,
    on_frame_body,
    on_frame_end
};

WebSocketParser::WebSocketParser() {
    parser = (websocket_parser*)malloc(sizeof(websocket_parser));
    websocket_parser_init(parser);
    parser->data = this;
    state = WS_FRAME_BEGIN;
    opcode = WS_OP_CLOSE;
    message_opcode = WS_OP_CLOSE;
    compression_negotiated = false;
    message_compressed = false;
    fragmented_message = false;
    frame_is_control = false;
}

WebSocketParser::~WebSocketParser() {
    if (parser) {
        free(parser);
        parser = NULL;
    }
}

int WebSocketParser::FeedRecvData(const char* data, size_t len) {
    return websocket_parser_execute(parser, &cbs, data, len);
}

void WebSocketParser::setCompression(const WebSocketCompressionOptions& options, bool negotiated, bool peer_is_server) {
    compression = options;
    compression_negotiated = negotiated && options.enabled;
    if (!compression_negotiated) {
        inflater_.reset();
        return;
    }
    if (!inflater_) {
        inflater_ = std::make_shared<hv::WebSocketInflater>();
    }
    int window_bits = peer_is_server ? compression.server_max_window_bits : compression.client_max_window_bits;
    bool no_context_takeover = peer_is_server ? compression.server_no_context_takeover : compression.client_no_context_takeover;
    if (inflater_->Init(window_bits, no_context_takeover, compression.max_decoded_size) != 0) {
        compression_negotiated = false;
        compression.enabled = false;
        inflater_.reset();
    }
}
