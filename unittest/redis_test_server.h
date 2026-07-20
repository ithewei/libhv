#ifndef UNITTEST_REDIS_TEST_SERVER_H_
#define UNITTEST_REDIS_TEST_SERVER_H_

#include <functional>
#include <memory>
#include <string>

#include "redis/RedisMessage.h"

class FakeRedisServer {
public:
    FakeRedisServer();
    ~FakeRedisServer();

    void setPort(int port);
    int port() const;

    void setCommandHandler(const std::function<hv::RedisReply(const hv::RedisCommand&)>& handler);
    void closeClientAfterReply(bool enabled);
    int publish(const std::string& channel, const std::string& message);

    void start();
    void stop();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

#endif // UNITTEST_REDIS_TEST_SERVER_H_
