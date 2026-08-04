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
//
// THREADING:
// - registerService() must be called BEFORE start(); the method table is read
//   from the IO thread(s) without locking, so registering after start() is a
//   data race.
// - A service method runs synchronously on the IO thread that owns the
//   connection. All calls on one connection are serialized on that thread, so a
//   slow handler blocks every other connection on the same IO thread. For heavy
//   work, hand off to your own worker thread/pool and reply from there.
class HV_EXPORT RpcServer : public TLVServer {
public:
    RpcServer(EventLoopPtr loop = NULL);
    virtual ~RpcServer();

    // NOTE: call before start() (see THREADING above).
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
