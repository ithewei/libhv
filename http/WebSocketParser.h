#ifndef HV_WEBSOCKET_PARSER_H_
#define HV_WEBSOCKET_PARSER_H_

#include "hexport.h"
#include "http_compress.h"

#include <string>
#include <memory>
#include <functional>

enum websocket_parser_state {
    WS_FRAME_BEGIN,
    WS_FRAME_HEADER,
    WS_FRAME_BODY,
    WS_FRAME_END,
    WS_FRAME_FIN,
};

struct websocket_parser;
class HV_EXPORT WebSocketParser {
public:
    websocket_parser*                   parser;
    websocket_parser_state              state;
    int                                 opcode;
    int                                 message_opcode;
    std::string                         message;
    std::string                         frame_message;
    std::function<void(int opcode, const std::string& msg)> onMessage;
    WebSocketCompressionOptions         compression;
    bool                                compression_negotiated;
    bool                                message_compressed;
    bool                                fragmented_message;
    bool                                frame_is_control;
    std::shared_ptr<hv::WebSocketInflater> inflater_;

    WebSocketParser();
    ~WebSocketParser();

    int FeedRecvData(const char* data, size_t len);
    void setCompression(const WebSocketCompressionOptions& options, bool negotiated = true, bool peer_is_server = true);
};

typedef std::shared_ptr<WebSocketParser> WebSocketParserPtr;

#endif // HV_WEBSOCKET_PARSER_H_
