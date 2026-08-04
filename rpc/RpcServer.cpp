#include "RpcServer.h"

#include "rpc.pb.h"

namespace hv {
namespace rpc {

RpcServer::RpcServer(EventLoopPtr loop) : TLVServer(loop) {
    tlv_setting_t setting = hrpc_tlv_setting();
    setTLV(&setting);
    onmessage = [this](const TLVChannelPtr& channel, const TLVMessage& tlv) {
        onFrame(channel, tlv);
    };
}

RpcServer::~RpcServer() {
    // Stop accepting/processing before RpcServer members (methods_, services_,
    // onmessage) are destroyed, so an in-flight onFrame can't touch freed
    // state (UAF). The base dtors also stop(), but only after our members die.
    stop();
}

void RpcServer::registerService(const RpcServicePtr& service) {
    // merge the service's methods into the router, keyed by the
    // fully-qualified "package.Service.Method" (matches the generated stub).
    for (auto& pair : service->methods()) {
        methods_[pair.first] = pair.second;
    }
    services_.push_back(service);
}

void RpcServer::onFrame(const TLVChannelPtr& channel, const TLVMessage& tlv) {
    HrpcMsgType msgtype;
    if (!hrpc_check_type(tlv, &msgtype)) return;

    if (msgtype == HRPC_PING) {
        sendControl(channel, HRPC_PONG);
        return;
    }
    if (msgtype == HRPC_CLOSE) {
        channel->close();
        return;
    }
    if (msgtype != HRPC_REQUEST) return;

    RpcMessage req;
    if (!req.ParseFromArray(tlv.value(), (int)tlv.length())) {
        // malformed envelope: the stream is out of sync, close so the peer's
        // in-flight calls fail promptly instead of waiting for a timeout.
        channel->close();
        return;
    }

    RpcMessage resp;
    resp.set_id(req.id());
    resp.set_method(req.method());

    auto iter = methods_.find(req.method());
    if (iter == methods_.end()) {
        resp.set_status(HRPC_STATUS_NOT_FOUND);
        resp.set_message("method not found: " + req.method());
    } else {
        std::string respPayload;
        RpcStatus st = iter->second(req.payload(), &respPayload);
        resp.set_status(st.code);
        if (!st.ok()) resp.set_message(st.message);
        else resp.set_payload(respPayload);
    }

    // send RESPONSE
    TLVMessage tlv_out;
    hrpc_set_type(&tlv_out, HRPC_RESPONSE);
    std::string body;
    resp.SerializeToString(&body);
    tlv_out.setValue(body);
    channel->sendTLV(tlv_out, &tlv_setting_);
}

void RpcServer::sendControl(const TLVChannelPtr& channel, HrpcMsgType msgtype) {
    TLVMessage tlv;
    hrpc_set_type(&tlv, msgtype);
    channel->sendTLV(tlv, &tlv_setting_);
}

} // namespace rpc
} // namespace hv
