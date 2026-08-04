#ifndef HV_RPC_PROTOCOL_H_
#define HV_RPC_PROTOCOL_H_

#include "TLVMessage.h"

namespace hv {
namespace rpc {

// hrpc frames the RPC envelope inside a TLV whose Type (8 bytes) is sliced:
//   type[0-3] magic "hrpc"
//   type[4]   version = 1
//   type[5]   message type (REQUEST/RESPONSE/PING/PONG/CLOSE)
//   type[6-7] reserved
#define HRPC_MAGIC          "hrpc"
#define HRPC_VERSION        1

#define HRPC_TYPE_BYTES     8
#define HRPC_LENGTH_BYTES   4

// type[5] values
enum HrpcMsgType {
    HRPC_REQUEST  = 0,
    HRPC_RESPONSE = 1,
    HRPC_PING     = 2,
    HRPC_PONG     = 3,
    HRPC_CLOSE    = 4,
};

// The tlv_setting shared by hrpc client and server.
static inline tlv_setting_t hrpc_tlv_setting() {
    tlv_setting_t s;
    s.type_bytes = HRPC_TYPE_BYTES;
    s.length_bytes = HRPC_LENGTH_BYTES;
    s.big_endian = true;
    return s;
}

// Fill a TLV Type header with magic + version + msg type.
static inline void hrpc_set_type(TLVMessage* tlv, HrpcMsgType msgtype) {
    tlv->setTypeAt(0, 'h');
    tlv->setTypeAt(1, 'r');
    tlv->setTypeAt(2, 'p');
    tlv->setTypeAt(3, 'c');
    tlv->setTypeAt(4, HRPC_VERSION);
    tlv->setTypeAt(5, (unsigned char)msgtype);
    tlv->setTypeAt(6, 0);
    tlv->setTypeAt(7, 0);
}

// Validate magic + version; returns msg type via out, or false if invalid.
static inline bool hrpc_check_type(const TLVMessage& tlv, HrpcMsgType* out) {
    if (tlv.typeAt(0) != 'h' || tlv.typeAt(1) != 'r' ||
        tlv.typeAt(2) != 'p' || tlv.typeAt(3) != 'c') {
        return false;
    }
    if (tlv.typeAt(4) != HRPC_VERSION) return false;
    if (out) *out = (HrpcMsgType)tlv.typeAt(5);
    return true;
}

// RPC call result (distinct from evpp/Status.h lifecycle status).
enum HrpcStatusCode {
    HRPC_STATUS_OK          = 0,
    HRPC_STATUS_TIMEOUT     = -1,
    HRPC_STATUS_NOT_CONNECTED = -2,
    HRPC_STATUS_NOT_FOUND   = -3,   // method not found (server)
    HRPC_STATUS_BAD_REQUEST = -4,   // payload parse error
    HRPC_STATUS_INTERNAL    = -5,   // handler failed
};

class RpcStatus {
public:
    int         code;
    std::string message;

    RpcStatus() : code(HRPC_STATUS_OK) {}
    RpcStatus(int c, const std::string& m = "") : code(c), message(m) {}

    bool ok() const { return code == HRPC_STATUS_OK; }
};

} // namespace rpc
} // namespace hv

#endif // HV_RPC_PROTOCOL_H_
