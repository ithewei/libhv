#ifndef HV_ASYNC_REDIS_CLIENT_HPP_
#define HV_ASYNC_REDIS_CLIENT_HPP_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "herr.h"

#include "EventLoopThread.h"
#include "RedisMessage.h"

namespace hv {

using RedisCallback = std::function<void(const RedisResult&)>;
using RedisRepliesCallback = std::function<void(int, const std::vector<RedisReply>&)>;

class HV_EXPORT AsyncRedisClient : private EventLoopThread {
public:
    AsyncRedisClient(EventLoopPtr loop = NULL);
    ~AsyncRedisClient();

    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);
    void setDb(int db);
    void setConnectTimeout(int ms);
    void setTimeout(int ms);
    void setReconnect(reconn_setting_t* setting);

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
