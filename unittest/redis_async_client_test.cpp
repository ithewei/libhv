#include "redis/AsyncRedisClient.h"
#include "EventLoop.h"
#include "redis_test_server.h"

#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace hv;

static void test_async_fifo_and_error_reply() {
    FakeRedisServer server;
    server.setCommandHandler([](const RedisCommand& cmd) {
        RedisReply reply;
        if (cmd[0] == "PING") {
            reply.type = REDIS_REPLY_STRING;
            reply.str = "PONG";
        }
        else {
            reply.type = REDIS_REPLY_ERROR;
            reply.str = "ERR unsupported";
        }
        return reply;
    });
    server.start();

    AsyncRedisClient client;
    client.setHost("127.0.0.1");
    client.setPort(server.port());
    client.start();

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<RedisResult> results;

    auto collect = [&](const RedisResult& result) {
        std::lock_guard<std::mutex> lock(mutex);
        results.push_back(result);
        cv.notify_one();
    };

    client.command(RedisCommand{"PING"}, collect);
    client.command(RedisCommand{"NOPE"}, collect);

    std::unique_lock<std::mutex> lock(mutex);
    bool done = cv.wait_for(lock, std::chrono::seconds(3), [&results]() { return results.size() == 2; });
    assert(done);
    assert(results[0].code == 0);
    assert(results[0].reply.asString() == "PONG");
    assert(results[1].code == 0);
    assert(results[1].reply.isError());
    assert(results[1].reply.error() == "ERR unsupported");

    client.stop(true);
    server.stop();
}


static void test_async_batch_replies() {
    FakeRedisServer server;
    server.setCommandHandler([](const RedisCommand& cmd) {
        RedisReply reply;
        if (cmd[0] == "PING") {
            reply.type = REDIS_REPLY_STRING;
            reply.str = "PONG";
        }
        else if (cmd[0] == "INCR") {
            reply.type = REDIS_REPLY_INTEGER;
            reply.integer = 1;
        }
        else {
            reply.type = REDIS_REPLY_ERROR;
            reply.str = "ERR unsupported";
        }
        return reply;
    });
    server.start();

    AsyncRedisClient client;
    client.setHost("127.0.0.1");
    client.setPort(server.port());
    client.start();

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    int code = ERR_UNKNOWN;
    std::vector<RedisReply> replies;

    client.commandBatch({RedisCommand{"PING"}, RedisCommand{"INCR", "counter"}},
                        [&](int batch_code, const std::vector<RedisReply>& batch_replies) {
                            std::lock_guard<std::mutex> lock(mutex);
                            code = batch_code;
                            replies = batch_replies;
                            done = true;
                            cv.notify_one();
                        });

    std::unique_lock<std::mutex> lock(mutex);
    bool completed = cv.wait_for(lock, std::chrono::seconds(3), [&done]() { return done; });
    assert(completed);
    assert(code == 0);
    assert(replies.size() == 2);
    assert(replies[0].asString() == "PONG");
    assert(replies[1].type == REDIS_REPLY_INTEGER);
    assert(replies[1].asInt() == 1);

    client.stop(true);
    server.stop();
}

static void test_command_without_start_is_rejected() {
    AsyncRedisClient client;

    bool called = false;
    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& result) {
        called = true;
        assert(result.code == ERR_CONNECT);
    });

    assert(ret == ERR_CONNECT);
    assert(called);

    bool batch_called = false;
    ret = client.commandBatch({RedisCommand{"PING"}}, [&](int code, const std::vector<RedisReply>& replies) {
        batch_called = true;
        assert(code == ERR_CONNECT);
        assert(replies.empty());
    });

    assert(ret == ERR_CONNECT);
    assert(batch_called);
}

static void test_command_after_stop_is_rejected() {
    AsyncRedisClient client;
    client.stop(true);

    bool called = false;
    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& result) {
        called = true;
        assert(result.code == ERR_CONNECT);
    });

    assert(ret == ERR_CONNECT);
    assert(called);
}

static void test_external_loop_mode_can_start_and_command() {
    FakeRedisServer server;
    server.setCommandHandler([](const RedisCommand& cmd) {
        RedisReply reply;
        if (cmd[0] == "PING") {
            reply.type = REDIS_REPLY_STRING;
            reply.str = "PONG";
        }
        else {
            reply.type = REDIS_REPLY_ERROR;
            reply.str = "ERR unsupported";
        }
        return reply;
    });
    server.start();

    EventLoopPtr loop = std::make_shared<EventLoop>();
    AsyncRedisClient client(loop);
    client.setHost("127.0.0.1");
    client.setPort(server.port());

    std::mutex mutex;
    std::condition_variable cv;
    bool connected = false;
    bool done = false;
    RedisResult result;

    client.onConnect = [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        connected = true;
        cv.notify_one();
    };

    loop->queueInLoop([&client]() {
        client.start(false);
    });
    std::thread loop_thread([&loop]() {
        loop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        bool started = cv.wait_for(lock, std::chrono::seconds(3), [&connected]() { return connected; });
        assert(started);
    }

    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& redis_result) {
        std::lock_guard<std::mutex> lock(mutex);
        result = redis_result;
        done = true;
        cv.notify_one();
    });
    assert(ret == 0);

    {
        std::unique_lock<std::mutex> lock(mutex);
        bool completed = cv.wait_for(lock, std::chrono::seconds(3), [&done]() { return done; });
        assert(completed);
    }
    assert(result.code == 0);
    assert(result.reply.asString() == "PONG");

    client.stop(true);
    loop->stop();
    loop_thread.join();
    server.stop();
}

static void test_external_loop_stopped_rejects_command() {
    EventLoopPtr loop = std::make_shared<EventLoop>();
    AsyncRedisClient client(loop);

    std::thread loop_thread([&loop]() {
        loop->run();
    });
    while (!loop->isRunning()) {
        hv_delay(1);
    }
    loop->stop();
    loop_thread.join();

    bool called = false;
    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& result) {
        called = true;
        assert(result.code == ERR_CONNECT);
    });

    assert(ret == ERR_CONNECT);
    assert(called);
}

static void test_external_loop_stopped_request_completes_once() {
    EventLoopPtr loop = std::make_shared<EventLoop>();
    AsyncRedisClient client(loop);

    std::thread loop_thread([&loop]() {
        loop->run();
    });
    while (!loop->isRunning()) {
        hv_delay(1);
    }

    std::atomic<int> callback_count(0);
    std::atomic<bool> stop_now(false);
    std::thread stop_thread([&]() {
        while (!stop_now.load()) {
            hv_delay(1);
        }
        loop->stop();
    });

    stop_now = true;
    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& result) {
        ++callback_count;
        assert(result.code == ERR_CONNECT || result.code == 0);
    });

    stop_thread.join();
    loop_thread.join();
    hv_delay(20);
    assert(callback_count.load() <= 1);
    assert(ret == ERR_CONNECT || ret == 0);
}

static void test_external_loop_not_running_rejects_start_and_command() {
    EventLoopPtr loop = std::make_shared<EventLoop>();
    AsyncRedisClient client(loop);

    int error_code = 0;
    client.onError = [&](int code) {
        error_code = code;
    };
    client.start(false);
    assert(error_code == ERR_CONNECT);

    int callback_count = 0;
    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& result) {
        ++callback_count;
        assert(result.code == ERR_CONNECT);
    });

    assert(ret == ERR_CONNECT);
    assert(callback_count == 1);
}

static void test_external_loop_running_without_start_rejects_command() {
    EventLoopPtr loop = std::make_shared<EventLoop>();
    AsyncRedisClient client(loop);

    std::thread loop_thread([&loop]() {
        loop->run();
    });
    while (!loop->isRunning()) {
        hv_delay(1);
    }

    int callback_count = 0;
    int ret = client.command(RedisCommand{"PING"}, [&](const RedisResult& result) {
        ++callback_count;
        assert(result.code == ERR_CONNECT);
    });
    assert(ret == ERR_CONNECT);
    assert(callback_count == 1);

    int batch_callback_count = 0;
    ret = client.commandBatch({RedisCommand{"PING"}}, [&](int code, const std::vector<RedisReply>& replies) {
        ++batch_callback_count;
        assert(code == ERR_CONNECT);
        assert(replies.empty());
    });
    assert(ret == ERR_CONNECT);
    assert(batch_callback_count == 1);

    loop->stop();
    loop_thread.join();
}

static void test_close_after_reply_fails_pending_once() {
    FakeRedisServer server;
    server.setCommandHandler([](const RedisCommand& cmd) {
        RedisReply reply;
        if (cmd[0] == "PING") {
            reply.type = REDIS_REPLY_STRING;
            reply.str = "PONG";
        }
        else {
            reply.type = REDIS_REPLY_ERROR;
            reply.str = "ERR unsupported";
        }
        return reply;
    });
    server.closeClientAfterReply(true);
    server.start();

    AsyncRedisClient client;
    client.setHost("127.0.0.1");
    client.setPort(server.port());
    client.start();

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<RedisResult> results;

    auto collect = [&](const RedisResult& result) {
        std::lock_guard<std::mutex> lock(mutex);
        results.push_back(result);
        cv.notify_one();
    };

    assert(client.command(RedisCommand{"PING"}, collect) == 0);
    assert(client.command(RedisCommand{"PING"}, collect) == 0);

    std::unique_lock<std::mutex> lock(mutex);
    bool completed = cv.wait_for(lock, std::chrono::seconds(3), [&results]() { return results.size() == 2; });
    assert(completed);
    assert(results[0].code == 0);
    assert(results[0].reply.asString() == "PONG");
    assert(results[1].code == ERR_CONNECT);

    client.stop(true);
    server.stop();
}

int main() {
    test_async_fifo_and_error_reply();
    test_async_batch_replies();
    test_command_without_start_is_rejected();
    test_command_after_stop_is_rejected();
    test_external_loop_mode_can_start_and_command();
    test_external_loop_stopped_rejects_command();
    test_external_loop_stopped_request_completes_once();
    test_external_loop_not_running_rejects_start_and_command();
    test_external_loop_running_without_start_rejects_command();
    test_close_after_reply_fails_pending_once();
    return 0;
}
