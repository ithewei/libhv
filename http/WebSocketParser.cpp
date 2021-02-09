#include "WebSocketParser.h"

#include "websocket_parser.h"

static int on_frame_header(websocket_parser* parser) {
    WebSocketParser* wp = (WebSocketParser*)parser->data;
    int opcode = parser->flags & WS_OP_MASK;
    // printf("on_frame_header opcode=%d\n", opcode);
    if (opcode != WS_OP_CONTINUE) {
        wp->opcode = opcode;
    }
    int length = parser->length;
    if (length && length > wp->message.capacity()) {
        wp->message.reserve(length);
    }
    if (wp->state == WS_FRAME_BEGIN ||
        wp->state == WS_FRAME_END) {
        wp->message.resize(0);
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
    wp->message.append(at, length);
    return 0;
}

static int on_frame_end(websocket_parser* parser) {
    // printf("on_frame_end\n");
    WebSocketParser* wp = (WebSocketParser*)parser->data;
    wp->state = WS_FRAME_END;
    if (wp->parser->flags & WS_FIN) {
        if (wp->onMessage) {
            wp->onMessage(wp->opcode, wp->message);
        }
    }
    return 0;
}

websocket_parser_settings* WebSocketParser::cbs = NULL;

WebSocketParser::WebSocketParser() {
    if (cbs == NULL) {
        cbs = (websocket_parser_settings*)malloc(sizeof(websocket_parser_settings));
        websocket_parser_settings_init(cbs);
        cbs->on_frame_header = on_frame_header;
        cbs->on_frame_body = on_frame_body;
        cbs->on_frame_end = on_frame_end;
    }
    parser = (websocket_parser*)malloc(sizeof(websocket_parser));
    websocket_parser_init(parser);
    parser->data = this;
    state = WS_FRAME_BEGIN;
}

WebSocketParser::~WebSocketParser() {
    if (parser) {
        free(parser);
        parser = NULL;
    }
}

int WebSocketParser::FeedRecvData(const char* data, size_t len) {
    return websocket_parser_execute(parser, cbs, data, len);
}
