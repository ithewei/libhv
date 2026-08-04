#ifndef HV_RPC_SERVICE_H_
#define HV_RPC_SERVICE_H_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "rpc_protocol.h"

namespace hv {
namespace rpc {

// Base class for a generated service. A generated CalcService subclass registers
// one method handler per rpc; each handler parses the request payload, invokes
// the user-implemented virtual, and serializes the reply payload.
//
//   status = handler(reqPayload, &respPayload)
//
// method key is the fully-qualified "package.Service.Method".
class RpcService {
public:
    // reqData -> respData; returns RpcStatus.
    typedef std::function<RpcStatus(const std::string& reqData, std::string* respData)> MethodHandler;

    virtual ~RpcService() {}

    // Fully-qualified service name, e.g. "hv.rpc.Calc". Provided by generated code.
    virtual const char* serviceName() const = 0;

    const std::map<std::string, MethodHandler>& methods() const { return methods_; }

protected:
    // Generated code calls this in its constructor for each rpc.
    void addMethod(const std::string& method, MethodHandler handler) {
        methods_[method] = std::move(handler);
    }

private:
    std::map<std::string, MethodHandler> methods_;
};

typedef std::shared_ptr<RpcService> RpcServicePtr;

} // namespace rpc
} // namespace hv

#endif // HV_RPC_SERVICE_H_
