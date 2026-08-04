#ifndef HV_RPC_CLIENT_H_
#define HV_RPC_CLIENT_H_

#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>

#include "hexport.h"
#include "TLVClient.h"

#include "rpc_protocol.h"

namespace hv {
namespace rpc {

#define HRPC_DEFAULT_PING_INTERVAL  3000  // ms; 0 disables heartbeat
#define HRPC_DEFAULT_CALL_TIMEOUT   10000 // ms
#define HRPC_MAX_MISSED_PONG        3

// RPC client over the TLV protocol. Correlates each RESPONSE to its REQUEST by
// id. Generated stubs call call()/callAsync() with a method name and serialized
// request; they parse the reply payload into a typed message.
//
// NOTE: RpcClient uses the inherited onConnection callback internally (to drive
// heartbeat and to fail pending calls on disconnect); do not override it.
class HV_EXPORT RpcClient : public TLVClient {
public:
    typedef std::function<void(const RpcStatus&, const std::string& respData)> AsyncCallback;

    RpcClient(EventLoopPtr loop = NULL);
    virtual ~RpcClient();

    // NOTE: call before start(). 0 disables heartbeat.
    void setPingInterval(int ms) { ping_interval_ = ms; }

    // Synchronous call. MUST NOT be called on this client's own loop thread
    // (it blocks on a future); intended for a separate caller thread.
    RpcStatus call(const std::string& method, const std::string& reqData,
                   std::string* respData, int timeout_ms = HRPC_DEFAULT_CALL_TIMEOUT);

    // Asynchronous call. Callback runs on the loop thread.
    void callAsync(const std::string& method, const std::string& reqData, AsyncCallback cb,
                   int timeout_ms = HRPC_DEFAULT_CALL_TIMEOUT);

    int sendPing();

private:
    struct Reply {
        RpcStatus   status;
        std::string data;
    };
    struct CallContext {
        std::promise<Reply> promise;
        AsyncCallback       cb;                        // set for async calls
        TimerID             timer = INVALID_TIMER_ID;  // async timeout timer
    };
    typedef std::shared_ptr<CallContext> CallContextPtr;

    uint64_t addCall(const CallContextPtr& ctx);
    void sendRequest(uint64_t id, const std::string& method, const std::string& reqData);
    void onFrame(const TLVChannelPtr& channel, const TLVMessage& tlv);
    void onCallTimeout(uint64_t id);
    void removeCall(uint64_t id);
    void failAllCalls(int code, const std::string& msg);
    void deliver(const CallContextPtr& ctx, const RpcStatus& status, const std::string& data);
    void onHeartbeat();

private:
    std::atomic<uint64_t>                       next_id_{0};
    std::map<uint64_t, CallContextPtr>          calls_;
    std::mutex                                  calls_mutex_;
    int                                         ping_interval_;
    int                                         ping_cnt_;
};

} // namespace rpc
} // namespace hv

#endif // HV_RPC_CLIENT_H_
