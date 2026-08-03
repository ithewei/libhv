#ifndef HV_RPC_CLIENT_H_
#define HV_RPC_CLIENT_H_

#include <atomic>
#include <future>
#include <map>
#include <mutex>
#include <vector>

#include "TLVClient.h"
#include "hlog.h"

#include "rpc_protocol.h"
#include "rpc.pb.h"

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
class RpcClient : public TLVClient {
public:
    typedef std::function<void(const RpcStatus&, const std::string& respData)> AsyncCallback;

    RpcClient(EventLoopPtr loop = NULL) : TLVClient(loop) {
        tlv_setting_t setting = hrpc_tlv_setting();
        setTLV(&setting);
        ping_interval_ = HRPC_DEFAULT_PING_INTERVAL;
        ping_cnt_ = 0;
        onmessage = [this](const TLVChannelPtr& channel, const TLVMessage& tlv) {
            onFrame(channel, tlv);
        };
        onConnection = [this](const TLVChannelPtr& channel) {
            if (channel->isConnected()) {
                // (re)start heartbeat on each successful connect
                if (ping_interval_ > 0) {
                    ping_cnt_ = 0;
                    channel->setHeartbeat(ping_interval_, [this]() { onHeartbeat(); });
                }
            } else {
                // disconnected: fail every in-flight call so no caller hangs
                failAllCalls(HRPC_STATUS_NOT_CONNECTED, "connection closed");
            }
        };
    }
    virtual ~RpcClient() {
        // Stop the loop thread before RpcClient members (calls_, onmessage) are
        // destroyed, so an in-flight onFrame can't touch freed state (UAF).
        // The base dtors also stop(), but only after our members are gone.
        stop();
        // any calls still pending after the loop stopped will never get a
        // response; fail them so a blocked sync caller wakes up.
        failAllCalls(HRPC_STATUS_NOT_CONNECTED, "client destroyed");
    }

    // NOTE: call before start(). 0 disables heartbeat.
    void setPingInterval(int ms) { ping_interval_ = ms; }

    // Synchronous call. MUST NOT be called on this client's own loop thread
    // (it blocks on a future); intended for a separate caller thread.
    RpcStatus call(const std::string& method, const std::string& reqData,
                   std::string* respData, int timeout_ms = HRPC_DEFAULT_CALL_TIMEOUT) {
        if (!isConnected()) return RpcStatus(HRPC_STATUS_NOT_CONNECTED, "not connected");

        auto ctx = std::make_shared<CallContext>();
        auto fut = ctx->promise.get_future();
        uint64_t id = addCall(ctx);            // no timer: wait_for bounds it
        sendRequest(id, method, reqData);

        if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
            removeCall(id);
            return RpcStatus(HRPC_STATUS_TIMEOUT, "rpc timeout");
        }
        Reply reply = fut.get();               // may be a disconnect failure
        if (reply.status.ok() && respData) *respData = reply.data;
        return reply.status;
    }

    // Asynchronous call. Callback runs on the loop thread.
    void callAsync(const std::string& method, const std::string& reqData, AsyncCallback cb,
                   int timeout_ms = HRPC_DEFAULT_CALL_TIMEOUT) {
        if (!isConnected()) {
            if (cb) cb(RpcStatus(HRPC_STATUS_NOT_CONNECTED, "not connected"), "");
            return;
        }
        auto ctx = std::make_shared<CallContext>();
        ctx->cb = std::move(cb);
        uint64_t id = ++next_id_;
        // Schedule the timeout timer BEFORE inserting into calls_ so a fast
        // response (loopback) that runs onFrame can always see ctx->timer, and
        // so the timer callback can't match an id that isn't tracked yet.
        if (timeout_ms > 0) {
            ctx->timer = loop()->setTimeout(timeout_ms, [this, id](TimerID) { onCallTimeout(id); });
        }
        {
            std::lock_guard<std::mutex> lock(calls_mutex_);
            calls_[id] = ctx;
        }
        sendRequest(id, method, reqData);
    }

    int sendPing() {
        TLVMessage tlv;
        hrpc_set_type(&tlv, HRPC_PING);
        return sendTLV(tlv);
    }

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

    uint64_t addCall(const CallContextPtr& ctx) {
        uint64_t id = ++next_id_;
        std::lock_guard<std::mutex> lock(calls_mutex_);
        calls_[id] = ctx;
        return id;
    }

    void sendRequest(uint64_t id, const std::string& method, const std::string& reqData) {
        RpcMessage msg;
        msg.set_id(id);
        msg.set_method(method);
        msg.set_payload(reqData);

        TLVMessage tlv;
        hrpc_set_type(&tlv, HRPC_REQUEST);
        std::string body;
        msg.SerializeToString(&body);
        tlv.setValue(body);
        sendTLV(tlv);
    }

    void onFrame(const TLVChannelPtr& channel, const TLVMessage& tlv) {
        HrpcMsgType msgtype;
        if (!hrpc_check_type(tlv, &msgtype)) return;
        if (msgtype == HRPC_PING) {
            TLVMessage pong;
            hrpc_set_type(&pong, HRPC_PONG);
            sendTLV(pong);
            return;
        }
        if (msgtype == HRPC_PONG) {
            ping_cnt_ = 0;
            return;
        }
        if (msgtype == HRPC_CLOSE) {
            if (channel) channel->close();
            return;
        }
        if (msgtype != HRPC_RESPONSE) return;

        RpcMessage resp;
        if (!resp.ParseFromArray(tlv.value(), (int)tlv.length())) return;

        CallContextPtr ctx;
        {
            std::lock_guard<std::mutex> lock(calls_mutex_);
            auto iter = calls_.find(resp.id());
            if (iter == calls_.end()) return;
            ctx = iter->second;
            calls_.erase(iter);
        }
        if (ctx->timer != INVALID_TIMER_ID) loop()->killTimer(ctx->timer);
        deliver(ctx, RpcStatus(resp.status(), resp.message()), resp.payload());
    }

    // async timeout fired on the loop thread
    void onCallTimeout(uint64_t id) {
        CallContextPtr ctx;
        {
            std::lock_guard<std::mutex> lock(calls_mutex_);
            auto iter = calls_.find(id);
            if (iter == calls_.end()) return;   // already answered
            ctx = iter->second;
            calls_.erase(iter);
        }
        deliver(ctx, RpcStatus(HRPC_STATUS_TIMEOUT, "rpc timeout"), "");
    }

    void removeCall(uint64_t id) {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        calls_.erase(id);
    }

    // Fail and drain every pending call. Drains under the lock, then delivers
    // outside it (a callback must not re-enter calls_).
    void failAllCalls(int code, const std::string& msg) {
        std::vector<CallContextPtr> pending;
        {
            std::lock_guard<std::mutex> lock(calls_mutex_);
            for (auto& kv : calls_) pending.push_back(kv.second);
            calls_.clear();
        }
        for (auto& ctx : pending) {
            if (ctx->timer != INVALID_TIMER_ID) loop()->killTimer(ctx->timer);
            deliver(ctx, RpcStatus(code, msg), "");
        }
    }

    void deliver(const CallContextPtr& ctx, const RpcStatus& status, const std::string& data) {
        if (ctx->cb) {
            ctx->cb(status, data);          // async
        } else {
            Reply reply;
            reply.status = status;
            reply.data = data;
            ctx->promise.set_value(reply);  // sync
        }
    }

    void onHeartbeat() {
        if (channel == NULL) return;
        if (ping_cnt_++ >= HRPC_MAX_MISSED_PONG) {
            hlogw("hrpc: no pong, closing connection");
            channel->close();
            return;
        }
        sendPing();
    }

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
