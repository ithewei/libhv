#ifndef HV_REDIS_PIPELINE_HPP_
#define HV_REDIS_PIPELINE_HPP_

#include <vector>

#include "AsyncRedisClient.h"

namespace hv {

class RedisClient;

class HV_EXPORT RedisPipeline {
public:
    explicit RedisPipeline(RedisClient* client = NULL);

    void appendCommand(const RedisCommand& command);
    RedisResult exec(std::vector<RedisReply>* replies = NULL);
    int execAsync(const RedisRepliesCallback& cb);

private:
    RedisClient* client_;
    std::vector<RedisCommand> commands_;
};

} // namespace hv

#endif // HV_REDIS_PIPELINE_HPP_
