#ifndef HV_RPC_SERVER_H_
#define HV_RPC_SERVER_H_

#include <map>
#include <string>
#include <vector>

#include "hexport.h"
#include "TLVServer.h"

#include "rpc_protocol.h"
#include "RpcService.h"

namespace hv {
namespace rpc {

// RPC server over the TLV protocol. Register generated services, then start().
// Incoming REQUEST frames are routed by RpcMessage.method to the matching
// service method; the reply is sent back as a RESPONSE frame. PING is answered
// with PONG; CLOSE closes the connection.
class HV_EXPORT RpcServer : public TLVServer {
public:
    RpcServer(EventLoopPtr loop = NULL);
    virtual ~RpcServer();

    void registerService(const RpcServicePtr& service);

private:
    void onFrame(const TLVChannelPtr& channel, const TLVMessage& tlv);
    void sendControl(const TLVChannelPtr& channel, HrpcMsgType msgtype);

private:
    std::map<std::string, RpcService::MethodHandler> methods_;
    std::vector<RpcServicePtr>                       services_;
};

} // namespace rpc
} // namespace hv

#endif // HV_RPC_SERVER_H_
