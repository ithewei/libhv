#ifndef HV_ASYNC_REDIS_CLIENT_HPP_
#define HV_ASYNC_REDIS_CLIENT_HPP_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "TcpClient.h"
#include "RedisMessage.h"

namespace hv {

using RedisCallback = std::function<void(const RedisResult&)>;
using RedisRepliesCallback = std::function<void(int, const std::vector<RedisReply>&)>;

// AsyncRedisClient is a coroutine-free async Redis client built directly on
// TcpClient: it reuses the base connect/reconnect/DNS/loop-ownership machinery
// (start/stop/setReconnect/setConnectTimeout are inherited) and only adds the
// RESP protocol layer (handshake, request pipelining, reply dispatch).
class HV_EXPORT AsyncRedisClient : public TcpClientTmpl<SocketChannel> {
public:
    AsyncRedisClient(EventLoopPtr loop = NULL);
    ~AsyncRedisClient();

    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);
    void setDb(int db);
    // per-request reply timeout (ms); connect timeout / reconnect are inherited
    // from TcpClient (setConnectTimeout / setReconnect).
    void setTimeout(int ms);

    // start/stop thread-safe; delegate to TcpClient after (re)setting redis state.
    void start(bool wait_threads_started = true);
    void stop(bool wait_threads_stopped = true);

    bool isConnected() const;
    bool isStarted() const;
    bool isInLoopThread();

    int command(const RedisCommand& command, RedisCallback cb);
    int commandBatch(const std::vector<RedisCommand>& commands, RedisRepliesCallback cb);

    std::function<void()> onConnect;
    std::function<void()> onClose;
    std::function<void(int)> onError;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hv

#endif // HV_ASYNC_REDIS_CLIENT_HPP_
