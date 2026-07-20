#include <cstdlib>
#include <iostream>
#include <string>

#include "redis/RedisClient.h"
#include "redis/RedisSubscriber.h"

using namespace hv;

int main(int argc, char** argv) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 6379;
    std::string channel = argc > 3 ? argv[3] : "libhv:redis:channel";

    RedisSubscriber subscriber;
    subscriber.setHost(host);
    subscriber.setPort(port);
    subscriber.onSubscribe = [&](const std::string& name) {
        std::cout << "subscribed: " << name << std::endl;
    };
    subscriber.onUnsubscribe = [&](const std::string& name) {
        std::cout << "unsubscribed: " << name << std::endl;
    };
    subscriber.onMessage = [&](const std::string& recv_channel, const std::string& message) {
        std::cout << recv_channel << " => " << message << std::endl;
    };
    subscriber.onError = [](int code) {
        std::cerr << "subscriber error: " << code << std::endl;
    };

    subscriber.start();
    int ret = subscriber.subscribe(channel);
    if (ret != 0) {
        std::cerr << "subscribe failed: " << ret << std::endl;
        return ret;
    }

    if (argc > 4) {
        RedisClient publisher;
        publisher.setHost(host);
        publisher.setPort(port);
        RedisValueResult<int64_t> published = publisher.publish(channel, argv[4]);
        if (!published.ok()) {
            std::cerr << "publish failed, code=" << published.code << std::endl;
        }
    }

    std::cout << "Press Enter to exit..." << std::endl;
    std::string line;
    std::getline(std::cin, line);
    subscriber.stop(true);
    return 0;
}
