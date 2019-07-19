#include "hplatform.h"

#ifdef OS_WIN
class WsaRAII {
public:
    WsaRAII() {
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2,2), &wsadata);
    }
    ~WsaRAII() {
        WSACleanup();
    }
};
static WsaRAII s_wsa;
#endif
