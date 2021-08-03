#ifndef HV_WEBSOCKET_PARSER_H_
#define HV_WEBSOCKET_PARSER_H_

#include "hexport.h"

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

struct websocket_parser_settings;
struct websocket_parser;
class HV_EXPORT WebSocketParser {
public:
    static websocket_parser_settings*   cbs;
    websocket_parser*                   parser;
    websocket_parser_state              state;
    int                                 opcode;
    std::string                         message;
    std::function<void(int opcode, const std::string& msg)> onMessage;

    WebSocketParser();
    ~WebSocketParser();

    int FeedRecvData(const char* data, size_t len);
};

typedef std::shared_ptr<WebSocketParser> WebSocketParserPtr;

#endif // HV_WEBSOCKET_PARSER_H_
