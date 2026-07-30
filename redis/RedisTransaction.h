#ifndef HV_REDIS_TRANSACTION_HPP_
#define HV_REDIS_TRANSACTION_HPP_

#include <vector>

#include "hexport.h"
#include "RedisMessage.h"

namespace hv {

class RedisClient;

class HV_EXPORT RedisTransaction {
public:
    explicit RedisTransaction(RedisClient* client = NULL);

    void appendCommand(const RedisCommand& command);
    RedisResult exec(std::vector<RedisReply>* replies = NULL);
    RedisResult discard();

private:
    RedisClient* client_;
    std::vector<RedisCommand> commands_;
};

} // namespace hv

#endif // HV_REDIS_TRANSACTION_HPP_
