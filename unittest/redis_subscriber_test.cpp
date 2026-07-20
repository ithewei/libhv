#include "redis/RedisSubscriber.h"
#include "redis_test_server.h"

#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

using namespace hv;

namespace {

struct SubscriberCapture {
    std::mutex mutex;
    std::condition_variable cv;
    size_t subscribed = 0;
    size_t unsubscribed = 0;
    std::vector<std::string> messages;
};

static RedisReply authSelectOnlyHandler(const RedisCommand& cmd) {
    if (!cmd.empty() && (cmd[0] == "AUTH" || cmd[0] == "SELECT")) {
        RedisReply reply;
        reply.type = REDIS_REPLY_STRING;
        reply.str = "OK";
        return reply;
    }
    RedisReply reply;
    reply.type = REDIS_REPLY_ERROR;
    reply.str = "ERR unsupported";
    return reply;
}

bool waitForCount(std::mutex& mutex, std::condition_variable& cv, size_t* count, size_t expected) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, std::chrono::seconds(3), [count, expected]() { return *count >= expected; });
}

void setupSubscriber(RedisSubscriber* subscriber, int port) {
    subscriber->setHost("127.0.0.1");
    subscriber->setPort(port);
    subscriber->setAuth("secret");
    subscriber->setDb(5);
}

void bindCallbacks(RedisSubscriber* subscriber, SubscriberCapture* capture, const std::string& name) {
    subscriber->onSubscribe = [capture, name](const std::string& subscribed_name) {
        std::lock_guard<std::mutex> lock(capture->mutex);
        if (subscribed_name == name) {
            ++capture->subscribed;
        }
        capture->cv.notify_one();
    };
    subscriber->onUnsubscribe = [capture, name](const std::string& unsubscribed_name) {
        std::lock_guard<std::mutex> lock(capture->mutex);
        if (unsubscribed_name == name) {
            ++capture->unsubscribed;
        }
        capture->cv.notify_one();
    };
    subscriber->onMessage = [capture](const std::string& channel, const std::string& message) {
        std::lock_guard<std::mutex> lock(capture->mutex);
        capture->messages.push_back(channel + ":" + message);
        capture->cv.notify_one();
    };
}

void expectPublishedMessage(SubscriberCapture* capture, const std::string& expected) {
    std::unique_lock<std::mutex> lock(capture->mutex);
    bool received = capture->cv.wait_for(lock, std::chrono::seconds(3), [capture]() { return capture->messages.size() == 1; });
    assert(received);
    assert(capture->messages[0] == expected);
}

void runSubscriptionFlow(const std::string& subscription_name,
                         const std::string& publish_channel,
                         const std::string& expected_message,
                         int (RedisSubscriber::*subscribe_fn)(const std::string&),
                         int (RedisSubscriber::*unsubscribe_fn)(const std::string&)) {
    FakeRedisServer server;
    server.setCommandHandler(authSelectOnlyHandler);
    server.start();

    RedisSubscriber subscriber;
    setupSubscriber(&subscriber, server.port());

    SubscriberCapture capture;
    bindCallbacks(&subscriber, &capture, subscription_name);

    subscriber.start();
    assert((subscriber.*subscribe_fn)(subscription_name) == 0);
    assert(waitForCount(capture.mutex, capture.cv, &capture.subscribed, 1));

    assert(server.publish(publish_channel, "hello") == 1);
    expectPublishedMessage(&capture, expected_message);

    assert((subscriber.*unsubscribe_fn)(subscription_name) == 0);
    assert(waitForCount(capture.mutex, capture.cv, &capture.unsubscribed, 1));
    assert(server.publish(publish_channel, "ignored") == 0);

    subscriber.stop(true);
    server.stop();
}

} // namespace

static void test_subscribe_and_message_callback() {
    runSubscriptionFlow("news", "news", "news:hello", &RedisSubscriber::subscribe, &RedisSubscriber::unsubscribe);
}

static void test_psubscribe_and_punsubscribe_callback() {
    runSubscriptionFlow("room:*", "room:1", "room:1:hello", &RedisSubscriber::psubscribe, &RedisSubscriber::punsubscribe);
}

static void test_subscribe_without_start_is_rejected() {
    RedisSubscriber subscriber;
    assert(subscriber.subscribe("late") == ERR_CONNECT);
    assert(subscriber.psubscribe("late:*") == ERR_CONNECT);
}

int main() {
    test_subscribe_and_message_callback();
    test_psubscribe_and_punsubscribe_callback();
    test_subscribe_without_start_is_rejected();
    return 0;
}
