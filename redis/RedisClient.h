#ifndef HV_REDIS_CLIENT_HPP_
#define HV_REDIS_CLIENT_HPP_

#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "AsyncRedisClient.h"
#include "RedisPipeline.h"
#include "RedisTransaction.h"

namespace hv {

template<typename T>
using RedisValueCallback = std::function<void(const RedisValueResult<T>&)>;

class HV_EXPORT RedisClient {
public:
    RedisClient();

    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);
    void setDb(int db);
    void setConnectTimeout(int ms);
    void setTimeout(int ms);
    void setReconnect(reconn_setting_t* setting);

    RedisResult command(const RedisCommand& command);
    int commandAsync(const RedisCommand& command, RedisCallback cb);
    RedisResult commandf(const char* fmt, ...);

    template<typename... Args>
    int commandfAsync(const char* fmt, RedisCallback cb, Args... args) {
        RedisCommand command;
        if (!formatCommandArgs(fmt, &command, args...)) {
            return ERR_INVALID_PARAM;
        }
        return commandAsync(command, std::move(cb));
    }

    RedisPipeline pipeline();
    RedisTransaction transaction();

    RedisValueResult<std::string> get(const std::string& key);
    int getAsync(const std::string& key, RedisValueCallback<std::string> cb);

    RedisResult set(const std::string& key, const std::string& value);
    int setAsync(const std::string& key, const std::string& value, RedisCallback cb);

    RedisValueResult<int64_t> del(const std::string& key);
    int delAsync(const std::string& key, RedisValueCallback<int64_t> cb);

    RedisValueResult<int64_t> exists(const std::string& key);
    int existsAsync(const std::string& key, RedisValueCallback<int64_t> cb);

    RedisValueResult<int64_t> expire(const std::string& key, int seconds);
    int expireAsync(const std::string& key, int seconds, RedisValueCallback<int64_t> cb);

    RedisValueResult<std::string> hget(const std::string& key, const std::string& field);
    int hgetAsync(const std::string& key, const std::string& field, RedisValueCallback<std::string> cb);

    RedisValueResult<int64_t> hset(const std::string& key, const std::string& field, const std::string& value);
    int hsetAsync(const std::string& key, const std::string& field, const std::string& value, RedisValueCallback<int64_t> cb);

    RedisValueResult<int64_t> publish(const std::string& channel, const std::string& message);
    int publishAsync(const std::string& channel, const std::string& message, RedisValueCallback<int64_t> cb);

private:
    template<typename... Args>
    static bool formatCommandArgs(const char* fmt, RedisCommand* command, Args... args) {
        if (fmt == nullptr || command == nullptr) {
            return false;
        }
        int len = std::snprintf(NULL, 0, fmt, args...);
        if (len < 0) {
            return false;
        }
        std::vector<char> buf((size_t)len + 1);
        if (std::snprintf(buf.data(), buf.size(), fmt, args...) < 0) {
            return false;
        }
        *command = tokenize(buf.data());
        return !command->empty();
    }

    static RedisCommand tokenize(const std::string& line);
    RedisResult commandBatch(const std::vector<RedisCommand>& commands, std::vector<RedisReply>* replies);
    int commandBatchAsync(const std::vector<RedisCommand>& commands, RedisRepliesCallback cb);

    friend class RedisPipeline;
    friend class RedisTransaction;

    AsyncRedisClient async_;
};

} // namespace hv

#endif // HV_REDIS_CLIENT_HPP_
