#ifndef HV_REDIS_SUBSCRIBER_HPP_
#define HV_REDIS_SUBSCRIBER_HPP_

#include <functional>
#include <memory>
#include <string>

#include "herr.h"

#include "EventLoopThread.h"

namespace hv {

class HV_EXPORT RedisSubscriber : private EventLoopThread {
public:
    RedisSubscriber(EventLoopPtr loop = NULL);
    ~RedisSubscriber();

    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);
    void setDb(int db);
    void setReconnect(reconn_setting_t* setting);

    void start(bool wait_threads_started = true);
    void stop(bool wait_threads_stopped = true);

    int subscribe(const std::string& channel);
    int psubscribe(const std::string& pattern);
    int unsubscribe(const std::string& channel);
    int punsubscribe(const std::string& pattern);

    std::function<void(const std::string&, const std::string&)> onMessage;
    std::function<void(const std::string&)> onSubscribe;
    std::function<void(const std::string&)> onUnsubscribe;
    std::function<void(int)> onError;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hv

#endif // HV_REDIS_SUBSCRIBER_HPP_
