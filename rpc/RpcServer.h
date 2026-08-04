#ifndef HV_RPC_SERVER_H_
#define HV_RPC_SERVER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TLVServer.h"

#include "rpc_protocol.h"
#include "RpcService.h"
#include "rpc.pb.h"

namespace hv {
namespace rpc {

// RPC server over the TLV protocol. Register generated services, then start().
// Incoming REQUEST frames are routed by RpcMessage.method to the matching
// service method; the reply is sent back as a RESPONSE frame. PING is answered
// with PONG; CLOSE closes the connection.
class RpcServer : public TLVServer {
public:
    RpcServer(EventLoopPtr loop = NULL) : TLVServer(loop) {
        tlv_setting_t setting = hrpc_tlv_setting();
        setTLV(&setting);
        onmessage = [this](const TLVChannelPtr& channel, const TLVMessage& tlv) {
            onFrame(channel, tlv);
        };
    }
    virtual ~RpcServer() {
        // Stop accepting/processing before RpcServer members (methods_, services_,
        // onmessage) are destroyed, so an in-flight onFrame can't touch freed
        // state (UAF). The base dtors also stop(), but only after our members die.
        stop();
    }

    void registerService(const RpcServicePtr& service) {
        // merge the service's methods into the router, keyed by the
        // fully-qualified "package.Service.Method" (matches the generated stub).
        for (auto& pair : service->methods()) {
            methods_[pair.first] = pair.second;
        }
        services_.push_back(service);
    }

private:
    void onFrame(const TLVChannelPtr& channel, const TLVMessage& tlv) {
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

        sendMessage(channel, HRPC_RESPONSE, resp);
    }

    void sendMessage(const TLVChannelPtr& channel, HrpcMsgType msgtype, const RpcMessage& msg) {
        TLVMessage tlv;
        hrpc_set_type(&tlv, msgtype);
        std::string body;
        msg.SerializeToString(&body);
        tlv.setValue(body);
        channel->sendTLV(tlv, &tlv_setting_);
    }

    void sendControl(const TLVChannelPtr& channel, HrpcMsgType msgtype) {
        TLVMessage tlv;
        hrpc_set_type(&tlv, msgtype);
        channel->sendTLV(tlv, &tlv_setting_);
    }

private:
    std::map<std::string, RpcService::MethodHandler> methods_;
    std::vector<RpcServicePtr>                       services_;
};

} // namespace rpc
} // namespace hv

#endif // HV_RPC_SERVER_H_
